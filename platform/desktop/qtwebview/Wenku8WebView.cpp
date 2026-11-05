// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8WebView.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QWebEngineDownloadItem>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QLockFile>
#include <QUuid>
#include <QWebEngineScriptCollection>

namespace Wenku8::QtWebView
{
    namespace
    {
        bool sameOrigin(const QUrl& oA, const QUrl& oB)
        {
            return oA.isValid() && oB.isValid() && oA.scheme() == oB.scheme() &&
                   oA.host() == oB.host() &&
                   oA.port(oA.scheme() == "https" ? 443 : 80) ==
                       oB.port(oB.scheme() == "https" ? 443 : 80);
        }
        void guiThread()
        {
            if (!qApp || QThread::currentThread() != qApp->thread())
                throw Error(ErrorCode::InvalidArgument,
                            "Browser session requires QApplication's thread");
        }
        class RestrictedPage final : public QWebEnginePage
        {
            QUrl m_oOrigin;

          public:
            RestrictedPage(QWebEngineProfile* pProfile, QUrl oOrigin)
                : QWebEnginePage(pProfile), m_oOrigin(std::move(oOrigin))
            {
                connect(this, &QWebEnginePage::featurePermissionRequested, this,
                        [this](const QUrl& oUrl, Feature eFeature)
                        {
                            setFeaturePermission(oUrl, eFeature, PermissionDeniedByUser);
                        });
            }

          protected:
            bool acceptNavigationRequest(const QUrl& oUrl, NavigationType, bool bMain) override
            {
                return sameOrigin(oUrl, m_oOrigin) || (!bMain && oUrl.scheme() == "https" &&
                                                       oUrl.host() == "challenges.cloudflare.com");
            }
            QWebEnginePage* createWindow(WebWindowType) override
            {
                return nullptr;
            }
            void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString&, int,
                                          const QString&) override
            {
            }
            void javaScriptAlert(const QUrl&, const QString&) override
            {
            }
            bool javaScriptConfirm(const QUrl&, const QString&) override
            {
                return false;
            }
            bool javaScriptPrompt(const QUrl&, const QString&, const QString&, QString*) override
            {
                return false;
            }
        };
        class ResourcePolicy final : public QWebEngineUrlRequestInterceptor
        {
            QUrl m_oOrigin;

          public:
            explicit ResourcePolicy(QUrl oOrigin, QObject* pParent)
                : QWebEngineUrlRequestInterceptor(pParent), m_oOrigin(std::move(oOrigin))
            {
            }
            void interceptRequest(QWebEngineUrlRequestInfo& oInfo) override
            {
                const auto oUrl = oInfo.requestUrl();
                const bool bAuxiliary =
                    oUrl.scheme() == "https" &&
                    (oUrl.host() == "challenges.cloudflare.com" || oUrl.host() == "img.wenku8.com");
                if (!sameOrigin(oUrl, m_oOrigin) && !bAuxiliary && oUrl.scheme() != "data" &&
                    oUrl != QUrl("about:blank"))
                    oInfo.block(true);
            }
        };
        struct Reply
        {
            std::mutex m_oMutex;
            std::condition_variable m_oChanged;
            bool m_bDone = false;
            HttpResponse m_oResponse;
            std::exception_ptr m_oError;
            std::atomic<bool> m_oAbort{false};
            std::atomic<std::uint64_t> m_oReceived{0}, m_oTotal{0};
        };
        void complete(const std::shared_ptr<Reply>& pReply, HttpResponse oResponse = {},
                      std::exception_ptr oError = {})
        {
            std::lock_guard<std::mutex> oLock(pReply->m_oMutex);
            if (pReply->m_bDone)
                return;
            pReply->m_oResponse = std::move(oResponse);
            pReply->m_oError = oError;
            pReply->m_bDone = true;
            pReply->m_oChanged.notify_all();
        }
        std::exception_ptr failure(ErrorCode eCode, const char* pMessage)
        {
            return std::make_exception_ptr(Error(eCode, pMessage));
        }
        QString json(const QJsonObject& oObject)
        {
            return QString::fromUtf8(QJsonDocument(oObject).toJson(QJsonDocument::Compact));
        }
        // ApplicationWorld isolates the job object from scripts in the login page.
        // fetch supplies browser cookies, actual HTTP status/headers and original bytes.
        const char* pFetchScript = R"JS(
(()=>{
 const c=CONFIG;
 if(location.origin!==new URL(c.url).origin) return false;
 const s={done:false,received:0,total:0,controller:new AbortController()};
 window.__wenku8SdkJob=s;
 (async()=>{
  try {
   const r=await fetch(c.url,{method:'GET',headers:c.headers,credentials:'same-origin',
       mode:'same-origin',cache:'no-store',redirect:c.follow?'follow':'manual',signal:s.controller.signal});
   if(r.type==='opaqueredirect') {s.error='redirect';return;}
   if(!r.status || new URL(r.url).origin!==location.origin) {s.error='origin';return;}
   s.status=r.status;s.url=r.url;s.headers=Array.from(r.headers.entries());
   s.total=Number(r.headers.get('content-length'))||0;
   const parts=[];const reader=r.body&&r.body.getReader();
   if(reader) for(;;){
    const x=await reader.read();if(x.done)break;
    s.received+=x.value.length;
    if(s.received>c.limit){s.error='size';await reader.cancel();return;}
    parts.push(x.value);
   }
   const encoded=[];
   let carry=[];
   for(const part of parts) for(let i=0;i<part.length;i++){
    carry.push(part[i]);
    if(carry.length===24576){encoded.push(btoa(String.fromCharCode.apply(null,carry)));carry=[];}
   }
   if(carry.length)encoded.push(btoa(String.fromCharCode.apply(null,carry)));
   s.body=encoded.join('');
  } catch(e){s.error='network';}
  finally{s.done=true;}
 })();
 return true;
})()
)JS";
    } // namespace

    class BrowserBridge final : public QObject
    {
        QWebEngineProfile* m_pProfile = nullptr;
        QPointer<QWebEnginePage> m_pPage;
        QPointer<QWebEngineView> m_pView;
        QTimer m_oPoll;
        std::shared_ptr<Reply> m_pActive;
        bool m_bPolling = false;
        QString m_strProfileRoot, m_strProfilePath, m_strPageStyle;
        std::unique_ptr<QLockFile> m_pProfileLock;
        void chooseProfile(bool bFresh)
        {
            if (m_strProfileRoot.isEmpty())
                return;
            QString strId;
            if (!bFresh)
            {
                QFile oSaved(QDir(m_strProfileRoot).filePath("active-profile"));
                if (oSaved.open(QIODevice::ReadOnly))
                    strId = QString::fromLatin1(oSaved.read(80)).trimmed();
            }
            if (QUuid(strId).isNull() || QUuid(strId).toString(QUuid::WithoutBraces) != strId)
            {
                strId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                QSaveFile oSaved(QDir(m_strProfileRoot).filePath("active-profile"));
                if (!oSaved.open(QIODevice::WriteOnly) ||
                    oSaved.write(strId.toLatin1()) != strId.size() || !oSaved.commit())
                    throw Error(ErrorCode::Io, "Cannot save local browser profile selection");
            }
            m_strProfilePath = QDir(m_strProfileRoot).filePath("profile-" + strId);
        }
        QString styleScript() const
        {
            QJsonObject oConfig{{"origin", m_oOrigin.toString(QUrl::RemovePath | QUrl::RemoveQuery |
                                                              QUrl::RemoveFragment)},
                                {"css", m_strPageStyle}};
            return QString(
                       "(()=>{const c=%1;if(location.origin!==c.origin)return;let "
                       "s=document.getElementById('wenku8-client-scrollbars');if(!s){s=document."
                       "createElement('style');s.id='wenku8-client-scrollbars';(document.head||"
                       "document.documentElement).appendChild(s);}s.textContent=c.css;})()")
                .arg(QString::fromUtf8(QJsonDocument(oConfig).toJson(QJsonDocument::Compact)));
        }
        void cleanup(const std::shared_ptr<Reply>& pReply)
        {
            if (m_pActive != pReply)
                return;
            m_oPoll.stop();
            m_pActive.reset();
            m_bPolling = false;
            if (m_pPage)
                m_pPage->runJavaScript("if(window.__wenku8SdkJob){window.__wenku8SdkJob.controller."
                                       "abort();delete window.__wenku8SdkJob;}",
                                       QWebEngineScript::ApplicationWorld);
        }
        void poll()
        {
            auto pReply = m_pActive;
            if (!pReply)
                return;
            if (pReply->m_oAbort)
            {
                complete(pReply, {}, failure(ErrorCode::Cancelled, "Browser request cancelled"));
                cleanup(pReply);
                return;
            }
            if (m_bPolling)
                return;
            if (!m_pPage)
            {
                complete(pReply, {}, failure(ErrorCode::Cancelled, "Browser page closed"));
                cleanup(pReply);
                return;
            }
            m_bPolling = true;
            m_pPage->runJavaScript(
                "(()=>{const s=window.__wenku8SdkJob;if(!s)return "
                "JSON.stringify({done:true,error:'navigation'});return "
                "JSON.stringify(s.done?{done:true,error:s.error,status:s.status,url:s.url,headers:"
                "s.headers,body:s.body,received:s.received,total:s.total}:{done:false,received:s."
                "received,total:s.total});})()",
                QWebEngineScript::ApplicationWorld,
                [this, pReply](const QVariant& oValue)
                {
                    if (m_pActive != pReply)
                        return;
                    m_bPolling = false;
                    auto oData = QJsonDocument::fromJson(oValue.toString().toUtf8()).object();
                    const auto fnBoundedCount = [](double fCount) -> std::uint64_t
                    {
                        return std::isfinite(fCount) && fCount >= 0 && fCount <= 9007199254740991.0
                                   ? static_cast<std::uint64_t>(fCount)
                                   : 0;
                    };
                    pReply->m_oReceived = fnBoundedCount(oData.value("received").toDouble());
                    pReply->m_oTotal = fnBoundedCount(oData.value("total").toDouble());
                    if (oData.isEmpty())
                    {
                        complete(pReply, {},
                                 failure(ErrorCode::Network, "Browser renderer unavailable"));
                        cleanup(pReply);
                        return;
                    }
                    if (!oData.value("done").toBool())
                        return;
                    const auto strError = oData.value("error").toString();
                    if (!strError.isEmpty())
                    {
                        auto eCode = strError == "size"       ? ErrorCode::ResponseTooLarge
                                     : strError == "redirect" ? ErrorCode::Unsupported
                                                              : ErrorCode::Network;
                        complete(
                            pReply, {},
                            failure(eCode,
                                    strError == "redirect"
                                        ? "Browser fetch cannot expose a manual redirect; open the "
                                          "destination in the login view"
                                        : "Browser fetch failed or exceeded its response limit"));
                    }
                    else
                    {
                        HttpResponse oResponse;
                        oResponse.m_nStatus = oData.value("status").toInt();
                        oResponse.m_strEffectiveUrl = oData.value("url").toString().toStdString();
                        oResponse.m_strBody =
                            QByteArray::fromBase64(oData.value("body").toString().toLatin1())
                                .toStdString();
                        for (const auto& oEntry : oData.value("headers").toArray())
                        {
                            auto oPair = oEntry.toArray();
                            if (oPair.size() == 2)
                                oResponse.m_vHeaders.emplace_back(
                                    oPair[0].toString().toStdString(),
                                    oPair[1].toString().toStdString());
                        }
                        complete(pReply, std::move(oResponse));
                    }
                    cleanup(pReply);
                });
        }

      public:
        QUrl m_oOrigin;
        std::atomic<bool> m_oClosed{false};
        BrowserBridge(QUrl oUrl, QWidget* pParent, const QString& strStorage)
            : m_oOrigin(std::move(oUrl))
        {
            if (!strStorage.isEmpty())
            {
                m_strProfileRoot = QDir(strStorage).absolutePath();
                if (!QDir().mkpath(m_strProfileRoot))
                    throw Error(ErrorCode::Io, "Cannot create local browser profile directory");
                m_pProfileLock =
                    std::make_unique<QLockFile>(QDir(m_strProfileRoot).filePath("profile.lock"));
                m_pProfileLock->setStaleLockTime(0);
                if (!m_pProfileLock->tryLock(0))
                {
                    m_pProfileLock.reset();
                    m_strProfileRoot.clear();
                }
            }
            m_pView = new QWebEngineView(pParent);
            connect(m_pView, &QObject::destroyed, this,
                    [this]
                    {
                        close();
                    });
            m_oPoll.setInterval(25);
            connect(&m_oPoll, &QTimer::timeout, this,
                    [this]
                    {
                        poll();
                    });
            reset(false);
        }
        ~BrowserBridge() override
        {
            close();
            auto pView = m_pView.data();
            m_pView = nullptr;
            if (pView)
            {
                disconnect(pView, nullptr, this, nullptr);
                delete pView;
            }
            auto pPage = m_pPage.data();
            m_pPage = nullptr;
            delete pPage;
            delete m_pProfile;
        }
        QWebEngineView* view() const
        {
            return m_pView;
        }
        std::string userAgent() const
        {
            return m_pProfile->httpUserAgent().toStdString();
        }
        void close()
        {
            if (m_oClosed.exchange(true))
                return;
            if (m_pActive)
            {
                auto pReply = m_pActive;
                complete(pReply, {}, failure(ErrorCode::Cancelled, "Browser session closed"));
                cleanup(pReply);
            }
            if (m_pView)
                m_pView->stop();
        }
        void reset(bool bForget = true)
        {
            if (m_oClosed)
                throw Error(ErrorCode::Cancelled, "Browser session closed");
            if (m_pActive)
            {
                auto pReply = m_pActive;
                complete(pReply, {}, failure(ErrorCode::Cancelled, "Browser session reset"));
                cleanup(pReply);
            }
            auto pOldPage = m_pPage;
            auto pOldProfile = m_pProfile;
            auto strOldPath = m_strProfilePath;
            chooseProfile(bForget);
            m_pProfile =
                m_strProfileRoot.isEmpty()
                    ? new QWebEngineProfile()
                    : new QWebEngineProfile("wenku8-" + QFileInfo(m_strProfilePath).fileName());
            if (!m_strProfileRoot.isEmpty())
            {
                m_pProfile->setPersistentStoragePath(m_strProfilePath);
                m_pProfile->setCachePath(QDir(m_strProfilePath).filePath("cache"));
            }
            m_pProfile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
            m_pProfile->setPersistentCookiesPolicy(m_strProfileRoot.isEmpty()
                                                       ? QWebEngineProfile::NoPersistentCookies
                                                       : QWebEngineProfile::ForcePersistentCookies);
            m_pProfile->setUrlRequestInterceptor(new ResourcePolicy(m_oOrigin, m_pProfile));
            connect(m_pProfile, &QWebEngineProfile::downloadRequested, this,
                    [](auto* pDownload)
                    {
                        pDownload->cancel();
                    });
            m_pPage = new RestrictedPage(m_pProfile, m_oOrigin);
            m_pPage->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, false);
            m_pPage->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
            m_pView->setPage(m_pPage);
            connect(m_pPage, &QWebEnginePage::loadStarted, this,
                    [this]
                    {
                        if (m_pActive)
                        {
                            auto pReply = m_pActive;
                            complete(
                                pReply, {},
                                failure(ErrorCode::Cancelled, "Browser navigated during request"));
                            cleanup(pReply);
                        }
                    });
            if (pOldPage)
                delete pOldPage;
            delete pOldProfile;
            const auto strOldResolved = QFileInfo(strOldPath).canonicalFilePath(),
                       strRootResolved = QFileInfo(m_strProfileRoot).canonicalFilePath();
            if (bForget && !strOldPath.isEmpty() && strOldPath != m_strProfilePath &&
                !strRootResolved.isEmpty() &&
                strOldResolved.startsWith(strRootResolved + "/profile-") &&
                QFileInfo(strOldResolved).absolutePath() == strRootResolved)
                QDir(strOldPath).removeRecursively();
            setPageStyle(m_strPageStyle);
        }
        bool persistent() const
        {
            return !m_strProfileRoot.isEmpty();
        }
        void setPageStyle(const QString& strCss)
        {
            m_strPageStyle = strCss;
            if (!m_pPage)
                return;
            auto oOld = m_pPage->scripts().findScript("wenku8-client-scrollbars");
            if (!oOld.isNull())
                m_pPage->scripts().remove(oOld);
            QWebEngineScript oScript;
            oScript.setName("wenku8-client-scrollbars");
            oScript.setInjectionPoint(QWebEngineScript::DocumentReady);
            oScript.setWorldId(QWebEngineScript::ApplicationWorld);
            oScript.setRunsOnSubFrames(false);
            oScript.setSourceCode(styleScript());
            m_pPage->scripts().insert(oScript);
            m_pPage->runJavaScript(styleScript(), QWebEngineScript::ApplicationWorld);
        }
        void openLogin()
        {
            if (m_oClosed)
                throw Error(ErrorCode::Cancelled, "Browser session closed");
            m_pView->load(m_oOrigin.resolved(QUrl("/login.php")));
        }
        void begin(const std::shared_ptr<Reply>& pReply, const HttpRequest& oRequest,
                   std::size_t nLimit, bool bFollow)
        {
            if (m_oClosed || pReply->m_oAbort)
            {
                complete(
                    pReply, {},
                    failure(ErrorCode::Cancelled, "Browser session closed or request cancelled"));
                return;
            }
            if (m_pActive && m_pActive->m_oAbort)
            {
                auto pOld = m_pActive;
                complete(pOld, {}, failure(ErrorCode::Cancelled, "Browser request cancelled"));
                cleanup(pOld);
            }
            if (m_pActive)
            {
                complete(pReply, {},
                         failure(ErrorCode::InvalidArgument, "Browser transport already busy"));
                return;
            }
            if (!m_pPage || !sameOrigin(m_pPage->url(), m_oOrigin))
            {
                complete(
                    pReply, {},
                    failure(ErrorCode::Authentication, "Open the website in the login view first"));
                return;
            }
            m_pActive = pReply;
            m_bPolling = false;
            QJsonObject oHeaders;
            for (const auto& oH : oRequest.m_vHeaders)
                if (QString::fromStdString(oH.first).compare("Accept", Qt::CaseInsensitive) == 0)
                    oHeaders.insert(QString::fromStdString(oH.first),
                                    QString::fromStdString(oH.second));
            QJsonObject oConfig{{"url", QString::fromStdString(oRequest.m_strUrl)},
                                {"limit", static_cast<double>(nLimit)},
                                {"headers", oHeaders},
                                {"follow", bFollow}};
            auto strScript = QString::fromUtf8(pFetchScript);
            strScript.replace("CONFIG", json(oConfig));
            m_pPage->runJavaScript(
                strScript, QWebEngineScript::ApplicationWorld,
                [this, pReply](const QVariant& oResult)
                {
                    if (m_pActive != pReply)
                        return;
                    if (!oResult.toBool())
                    {
                        complete(pReply, {},
                                 failure(ErrorCode::Network, "Browser origin unavailable"));
                        cleanup(pReply);
                        return;
                    }
                    m_oPoll.start();
                });
        }
    };

    namespace
    {
        class BrowserTransport final : public HttpTransport
        {
            std::shared_ptr<BrowserBridge> m_pBridge;
            HttpResponse wait(const std::shared_ptr<Reply>& pReply, const TransferOptions& oOptions)
            {
                const auto oUntil = std::chrono::steady_clock::now() + oOptions.m_oTimeout;
                for (;;)
                {
                    if (m_pBridge->m_oClosed)
                    {
                        pReply->m_oAbort = true;
                        complete(pReply, {},
                                 failure(ErrorCode::Cancelled, "Browser session closed"));
                    }
                    if (std::chrono::steady_clock::now() >= oUntil)
                    {
                        pReply->m_oAbort = true;
                        complete(pReply, {},
                                 failure(ErrorCode::Network, "Browser request timed out"));
                    }
                    try
                    {
                        if (oOptions.m_fnProgress &&
                            !oOptions.m_fnProgress(pReply->m_oReceived, pReply->m_oTotal))
                        {
                            pReply->m_oAbort = true;
                            complete(pReply, {},
                                     failure(ErrorCode::Cancelled, "Browser request cancelled"));
                        }
                    }
                    catch (...)
                    {
                        pReply->m_oAbort = true;
                        complete(pReply, {},
                                 failure(ErrorCode::Network, "Browser progress callback failed"));
                    }
                    std::unique_lock<std::mutex> oLock(pReply->m_oMutex);
                    if (pReply->m_bDone)
                    {
                        if (pReply->m_oError)
                            std::rethrow_exception(pReply->m_oError);
                        return std::move(pReply->m_oResponse);
                    }
                    pReply->m_oChanged.wait_for(oLock, std::chrono::milliseconds(25));
                }
            }

          public:
            explicit BrowserTransport(std::shared_ptr<BrowserBridge> pBridge)
                : m_pBridge(std::move(pBridge))
            {
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return true;
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                if (QThread::currentThread() == m_pBridge->thread())
                    throw Error(ErrorCode::InvalidArgument,
                                "Call browser transport from a worker thread");
                const auto oUrl = QUrl(QString::fromStdString(oRequest.m_strUrl), QUrl::StrictMode);
                if (!sameOrigin(oUrl, m_pBridge->m_oOrigin) || !oUrl.userInfo().isEmpty() ||
                    oUrl.hasFragment() || oRequest.m_strUrl.find('\0') != std::string::npos)
                    throw Error(ErrorCode::InvalidArgument,
                                "Browser transport requires a same-origin URL");
                if (oRequest.m_strMethod != "GET" || !oRequest.m_strBody.empty())
                    throw Error(
                        ErrorCode::Unsupported,
                        "Browser transport supports read-only GET; sign in through the view");
                if (oOptions.m_oTimeout.count() <= 0 ||
                    oOptions.m_oTimeout > std::chrono::hours(24) || !oOptions.m_nMaxResponseBytes ||
                    oOptions.m_nMaxResponseBytes > 64ULL * 1024 * 1024)
                    throw Error(ErrorCode::InvalidArgument,
                                "Invalid browser transfer limits (maximum 64 MiB)");
                for (const auto& oH : oRequest.m_vHeaders)
                {
                    auto strName = QString::fromStdString(oH.first).toLower();
                    if ((strName != "accept" && strName != "user-agent" && strName != "referer") ||
                        oH.second.find_first_of("\r\n") != std::string::npos ||
                        oH.second.find('\0') != std::string::npos)
                        throw Error(ErrorCode::Unsupported,
                                    "Browser-managed or unsupported request header");
                }
                auto pReply = std::make_shared<Reply>();
                QMetaObject::invokeMethod(
                    m_pBridge.get(),
                    [pBridge = m_pBridge, pReply, oRequest, nLimit = oOptions.m_nMaxResponseBytes,
                     bFollow = oOptions.m_bFollowRedirects]
                    {
                        pBridge->begin(pReply, oRequest, nLimit, bFollow);
                    },
                    Qt::QueuedConnection);
                return wait(pReply, oOptions);
            }
            void clearCookies() override
            {
                if (QThread::currentThread() == m_pBridge->thread())
                {
                    m_pBridge->reset();
                    return;
                }
                auto pReply = std::make_shared<Reply>();
                QMetaObject::invokeMethod(
                    m_pBridge.get(),
                    [pBridge = m_pBridge, pReply]
                    {
                        if (pReply->m_oAbort)
                        {
                            complete(pReply, {},
                                     failure(ErrorCode::Cancelled, "Browser reset cancelled"));
                            return;
                        }
                        try
                        {
                            pBridge->reset();
                            complete(pReply);
                        }
                        catch (...)
                        {
                            complete(pReply, {}, std::current_exception());
                        }
                    },
                    Qt::QueuedConnection);
                TransferOptions oLimits;
                oLimits.m_oTimeout = std::chrono::seconds(5);
                wait(pReply, oLimits);
            }
        };
    } // namespace
    BrowserSession::BrowserSession(const QUrl& oOrigin, QWidget* pParent, const QString& strStorage)
    {
        guiThread();
        const bool bLoopback = oOrigin.scheme() == "http" &&
                               (oOrigin.host() == "127.0.0.1" || oOrigin.host() == "localhost" ||
                                oOrigin.host() == "::1");
        if (!oOrigin.isValid() || oOrigin.host().isEmpty() ||
            (oOrigin.scheme() != "https" && !bLoopback) || !oOrigin.userInfo().isEmpty() ||
            oOrigin.hasQuery() || oOrigin.hasFragment() ||
            (!oOrigin.path().isEmpty() && oOrigin.path() != "/"))
            throw Error(ErrorCode::InvalidArgument,
                        "Browser requires an HTTPS origin (HTTP loopback only)");
        m_pBridge =
            std::shared_ptr<BrowserBridge>(new BrowserBridge(oOrigin, pParent, strStorage),
                                           [](BrowserBridge* pBridge)
                                           {
                                               if (QThread::currentThread() == pBridge->thread())
                                                   delete pBridge;
                                               else
                                                   pBridge->deleteLater();
                                           });
    }
    BrowserSession::~BrowserSession()
    {
        close();
    }
    QWebEngineView* BrowserSession::view() const
    {
        return m_pBridge->view();
    }
    std::string BrowserSession::userAgent() const
    {
        guiThread();
        return m_pBridge->userAgent();
    }
    bool BrowserSession::persistent() const
    {
        guiThread();
        return m_pBridge->persistent();
    }
    void BrowserSession::setPageStyle(const QString& strCss)
    {
        guiThread();
        m_pBridge->setPageStyle(strCss);
    }
    void BrowserSession::openLogin()
    {
        guiThread();
        m_pBridge->openLogin();
    }
    void BrowserSession::reset()
    {
        guiThread();
        m_pBridge->reset();
    }
    void BrowserSession::close()
    {
        guiThread();
        m_pBridge->close();
    }
    std::shared_ptr<HttpTransport> BrowserSession::transport() const
    {
        return std::make_shared<BrowserTransport>(m_pBridge);
    }
} // namespace Wenku8::QtWebView
