// SPDX-License-Identifier: GPL-2.0-only
#include "LoginHost.h"
#include "MobileBrowserProtocol.h"
#include <QtAndroid>
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QApplication>
#include <QWidget>
#include <QThread>
#include <QPointer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <jni.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
static void initMobileResources()
{
    Q_INIT_RESOURCE(mobile_browser);
}
namespace Wenku8::Reader
{
    namespace
    {
        constexpr auto pJavaClass = "io/github/lucky_ji/wenku8_reader/BrowserLogin";
        struct Pending
        {
            std::mutex m_oMutex;
            std::condition_variable m_oReady;
            bool m_bDone = false;
            QByteArray m_baJson;
            quint64 m_nToken = 0;
        };
        std::mutex oRegistryMutex;
        std::map<quint64, std::shared_ptr<Pending>> vPending;
        std::map<quint64, QPointer<LoginHost>> vHosts; // Accessed only on Qt's UI thread.
        std::atomic<quint64> oNextId{1};
        bool jniOK()
        {
            QAndroidJniEnvironment oEnv;
            if (oEnv->ExceptionCheck())
            {
                oEnv->ExceptionClear();
                return false;
            }
            return true;
        }
        void command(quint64 nToken, int nAction, const QString& strText = {})
        {
            auto oValue = QAndroidJniObject::fromString(strText);
            QAndroidJniObject::callStaticMethod<void>(pJavaClass, "command",
                                                      "(JILjava/lang/String;)V", jlong(nToken),
                                                      jint(nAction), oValue.object<jstring>());
            jniOK();
        }
        void finish(const std::shared_ptr<Pending>& pP, const QByteArray& baData)
        {
            std::lock_guard<std::mutex> oLock(pP->m_oMutex);
            if (pP->m_bDone)
                return;
            pP->m_bDone = true;
            pP->m_baJson = baData;
            pP->m_oReady.notify_all();
        }
        struct Session
        {
            QUrl m_oOrigin;
            quint64 m_nToken;
            std::atomic<bool> m_oClosed{false};
            std::timed_mutex m_oSerial;
        };
        class AndroidTransport final : public HttpTransport
        {
            std::shared_ptr<Session> m_pState;

          public:
            explicit AndroidTransport(std::shared_ptr<Session> pS) : m_pState(std::move(pS))
            {
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return true;
            }
            void clearCookies() override
            {
                throw Error(ErrorCode::Unsupported,
                            "Use Switch Account to reset the browser session");
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oLimits) override
            {
                if (!qApp || QThread::currentThread() == qApp->thread())
                    throw Error(ErrorCode::InvalidArgument, "Call mobile transport on a worker");
                const auto oConfig =
                    MobileBrowserProtocol::requestConfig(m_pState->m_oOrigin, oRequest, oLimits);
                auto oDeadline = std::chrono::steady_clock::now() + oLimits.m_oTimeout;
                auto fnCheck = [&]
                {
                    if (m_pState->m_oClosed ||
                        (oLimits.m_fnProgress && !oLimits.m_fnProgress(0, 0)))
                        throw Error(ErrorCode::Cancelled, "Mobile browser request cancelled");
                    if (std::chrono::steady_clock::now() >= oDeadline)
                        throw Error(ErrorCode::Network, "Mobile browser request timed out");
                };
                std::unique_lock<std::timed_mutex> oSerial(m_pState->m_oSerial, std::defer_lock);
                while (!oSerial.try_lock_for(std::chrono::milliseconds(25)))
                    fnCheck();
                fnCheck();
                QFile oScriptFile(":/mobile-browser/mobile_fetch.js");
                if (!oScriptFile.open(QIODevice::ReadOnly))
                    throw Error(ErrorCode::Io, "Mobile request script unavailable");
                auto strScript = QString::fromUtf8(oScriptFile.readAll());
                strScript.replace("CONFIG", QString::fromUtf8(QJsonDocument(oConfig).toJson(
                                                QJsonDocument::Compact)));
                const auto nId = oNextId.fetch_add(1);
                auto pP = std::make_shared<Pending>();
                pP->m_nToken = m_pState->m_nToken;
                {
                    std::lock_guard<std::mutex> oLock(oRegistryMutex);
                    vPending[nId] = pP;
                }
                try
                {
                    auto oSource = QAndroidJniObject::fromString(strScript);
                    QAndroidJniObject::callStaticMethod<void>(
                        pJavaClass, "fetch", "(JJLjava/lang/String;)V", jlong(m_pState->m_nToken),
                        jlong(nId), oSource.object<jstring>());
                    if (!jniOK())
                        throw Error(ErrorCode::Network, "Cannot dispatch mobile browser request");
                    std::unique_lock<std::mutex> oLock(pP->m_oMutex);
                    while (!pP->m_bDone)
                    {
                        pP->m_oReady.wait_for(oLock, std::chrono::milliseconds(25));
                        oLock.unlock();
                        fnCheck();
                        oLock.lock();
                    }
                    auto baBytes = pP->m_baJson;
                    oLock.unlock();
                    {
                        std::lock_guard<std::mutex> oRegistryLock(oRegistryMutex);
                        vPending.erase(nId);
                    }
                    auto oResponse = MobileBrowserProtocol::response(m_pState->m_oOrigin, baBytes,
                                                                     oLimits.m_nMaxResponseBytes);
                    if (oLimits.m_fnProgress && !oLimits.m_fnProgress(oResponse.m_strBody.size(),
                                                                      oResponse.m_strBody.size()))
                        throw Error(ErrorCode::Cancelled, "Mobile browser request cancelled");
                    return oResponse;
                }
                catch (...)
                {
                    {
                        std::lock_guard<std::mutex> oLock(oRegistryMutex);
                        vPending.erase(nId);
                    }
                    command(m_pState->m_nToken, 6);
                    throw;
                }
            }
        };
        class AndroidLoginHost final : public LoginHost
        {
            std::shared_ptr<Session> m_pState = std::make_shared<Session>();
            QPointer<QWidget> m_pOwner;
            bool m_bVisible = false, m_bPage = false;
            std::string m_strAgent;

          public:
            AndroidLoginHost(QWidget* pOwner, const QUrl& oOrigin) : m_pOwner(pOwner)
            {
                MobileBrowserProtocol::validateOrigin(oOrigin);
                initMobileResources();
                m_pState->m_oOrigin = oOrigin;
                m_pState->m_nToken = oNextId.fetch_add(1);
                vHosts[m_pState->m_nToken] = this;
                auto oSite = QAndroidJniObject::fromString(
                    oOrigin.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment));
                QFile oFile(":/mobile-browser/mobile_navigation.js");
                if (!oFile.open(QIODevice::ReadOnly))
                {
                    vHosts.erase(m_pState->m_nToken);
                    throw Error(ErrorCode::Io, "Mobile navigation script unavailable");
                }
                const auto baEncoded =
                    QJsonDocument(QJsonArray{oOrigin.toString(QUrl::RemovePath | QUrl::RemoveQuery |
                                                              QUrl::RemoveFragment)})
                        .toJson(QJsonDocument::Compact);
                auto strScript = QString::fromUtf8(oFile.readAll());
                QFile oRemember(":/mobile-browser/remember_login.js");
                if (oRemember.open(QIODevice::ReadOnly))
                    strScript += "\n;\n" + QString::fromUtf8(oRemember.readAll());
                QFile oPresentation(":/mobile-browser/login_ui.js");
                if (oPresentation.open(QIODevice::ReadOnly))
                    strScript += "\n;\n" + QString::fromUtf8(oPresentation.readAll());
                strScript.replace("ORIGIN",
                                  QString::fromUtf8(baEncoded.mid(1, baEncoded.size() - 2)));
                auto oGuard = QAndroidJniObject::fromString(strScript);
                QAndroidJniObject::callStaticMethod<void>(
                    pJavaClass, "create",
                    "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;J)V",
                    QtAndroid::androidActivity().object<jobject>(), oSite.object<jstring>(),
                    oGuard.object<jstring>(), jlong(m_pState->m_nToken));
                if (!jniOK())
                {
                    vHosts.erase(m_pState->m_nToken);
                    throw Error(ErrorCode::Unsupported, "Android WebView host unavailable");
                }
            }
            ~AndroidLoginHost() override
            {
                close();
                vHosts.erase(m_pState->m_nToken);
            }
            void event(int nType, const QString& strValue)
            {
                if (m_pState->m_oClosed)
                    return;
                if (nType == 0)
                    m_strAgent = strValue.toStdString();
                else if (nType == 1)
                {
                    m_bPage = true;
                    emit pageLoaded();
                }
                else if (nType == 2)
                {
                    m_bVisible = false;
                    emit cancelled();
                }
                else if (nType == 3)
                    emit checkRequested();
                else if (nType == 4)
                {
                    m_bVisible = false;
                    m_bPage = false;
                    emit unavailable();
                }
                else if (nType == 5)
                    m_bPage = false;
                else if (nType == 6 && !m_bVisible && m_pOwner)
                {
                    // Acknowledge the native overlay's asynchronous removal only
                    // after Android has returned focus to the Qt content view.
                    m_pOwner->activateWindow();
                    m_pOwner->setFocus(Qt::OtherFocusReason);
                    m_pOwner->update();
                }
            }
            bool visible() const override
            {
                return m_bVisible;
            }
            bool hasPage() const override
            {
                return m_bPage;
            }
            bool persistent() const override
            {
                return true;
            }
            void show(bool bReset) override
            {
                bool bOpening = !m_bVisible;
                m_bVisible = true;
                command(m_pState->m_nToken, 0);
                if (bReset)
                {
                    m_bPage = false;
                    command(m_pState->m_nToken, 4);
                }
                else if (bOpening || !m_bPage)
                    openLogin();
            }
            void openLogin() override
            {
                m_bPage = false;
                command(m_pState->m_nToken, 1);
            }
            void accept() override
            {
                m_bVisible = false;
                command(m_pState->m_nToken, 2);
            }
            void setHint(const QString& strHint) override
            {
                command(m_pState->m_nToken, 3, strHint);
            }
            void setTheme(Theme) override
            {
            } // Login is system WebView; app themes remain native Qt.
            std::string userAgent() const override
            {
                return m_strAgent;
            }
            std::shared_ptr<HttpTransport> transport() const override
            {
                return std::make_shared<AndroidTransport>(m_pState);
            }
            void close() override
            {
                if (m_pState->m_oClosed.exchange(true))
                    return;
                m_bVisible = false;
                {
                    std::lock_guard<std::mutex> oLock(oRegistryMutex);
                    for (const auto& oPair : vPending)
                        if (oPair.second->m_nToken == m_pState->m_nToken)
                            finish(oPair.second, "{\"done\":true,\"error\":\"cancel\"}");
                }
                command(m_pState->m_nToken, 5);
            }
        };
    } // namespace
    std::unique_ptr<LoginHost> makeLoginHost(QWidget* pOwner, const QUrl& oOrigin, const QString&)
    {
        return std::make_unique<AndroidLoginHost>(pOwner, oOrigin);
    }
    void mobileHostEvent(quint64 nToken, int nEvent, const QString& strData)
    {
        auto oIt = vHosts.find(nToken);
        if (oIt != vHosts.end() && oIt->second)
            static_cast<AndroidLoginHost*>(oIt->second.data())->event(nEvent, strData);
    }
    void mobileHostReply(quint64 nToken, quint64 nId, const QByteArray& baData)
    {
        std::shared_ptr<Pending> pReply;
        {
            std::lock_guard<std::mutex> oLock(oRegistryMutex);
            auto oIt = vPending.find(nId);
            if (oIt != vPending.end() && oIt->second->m_nToken == nToken)
                pReply = oIt->second;
        }
        if (pReply)
            finish(pReply, baData);
    }
} // namespace Wenku8::Reader
extern "C" JNIEXPORT void JNICALL Java_io_github_lucky_1ji_wenku8_1reader_BrowserLogin_nativeEvent(
    JNIEnv* pEnv, jclass, jlong nToken, jint nEvent, jstring pData)
{
    if (!qApp)
        return;
    const auto pChars = pEnv->GetStringChars(pData, nullptr);
    if (!pChars)
        return;
    QString strValue =
        QString::fromUtf16(reinterpret_cast<const ushort*>(pChars), pEnv->GetStringLength(pData));
    pEnv->ReleaseStringChars(pData, pChars);
    QMetaObject::invokeMethod(
        qApp,
        [nToken, nEvent, strValue]
        {
            Wenku8::Reader::mobileHostEvent(quint64(nToken), nEvent, strValue);
        },
        Qt::QueuedConnection);
}
extern "C" JNIEXPORT void JNICALL Java_io_github_lucky_1ji_wenku8_1reader_BrowserLogin_nativeReply(
    JNIEnv* pEnv, jclass, jlong nToken, jlong nId, jstring pData)
{
    if (!pData || pEnv->GetStringLength(pData) > 46 * 1024 * 1024)
        return;
    const auto pChars = pEnv->GetStringChars(pData, nullptr);
    if (!pChars)
        return;
    auto baBytes =
        QString::fromUtf16(reinterpret_cast<const ushort*>(pChars), pEnv->GetStringLength(pData))
            .toUtf8();
    pEnv->ReleaseStringChars(pData, pChars);
    Wenku8::Reader::mobileHostReply(quint64(nToken), quint64(nId), baBytes);
}
