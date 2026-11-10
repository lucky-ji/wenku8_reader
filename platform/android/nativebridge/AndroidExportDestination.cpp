// SPDX-License-Identifier: GPL-2.0-only
#include "ExportDestination.h"
#include "Wenku8Sdk.h"
#include <QtAndroid>
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
namespace
{
    constexpr auto pJavaClass = "io/github/lucky_ji/wenku8_reader/ExportDocuments";
    std::atomic<jlong> oNextToken{1};
    std::atomic<int> oNextRequest{0x6240};
    std::mutex oCallbacksMutex;
    std::map<jlong, std::function<bool()>> vCallbacks;
    bool jniOK()
    {
        QAndroidJniEnvironment oEnvironment;
        if (oEnvironment->ExceptionCheck())
        {
            oEnvironment->ExceptionClear();
            return false;
        }
        return true;
    }
    QString javaString(const char* pMethod, const QString& strLocation)
    {
        const auto oLocation = QAndroidJniObject::fromString(strLocation);
        const auto oResult = QAndroidJniObject::callStaticObjectMethod(
            pJavaClass, pMethod, "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            QtAndroid::androidContext().object(), oLocation.object<jstring>());
        return jniOK() && oResult.isValid() ? oResult.toString() : QString();
    }
    QJsonObject parseResult(const QAndroidJniObject& oResult)
    {
        if (!jniOK() || !oResult.isValid())
        {
            throw Wenku8::Error(Wenku8::ErrorCode::Io, "无法访问所选目录，请重新选择保存位置。");
        }
        QJsonParseError oError;
        const auto oDocument = QJsonDocument::fromJson(oResult.toString().toUtf8(), &oError);
        if (oError.error != QJsonParseError::NoError || !oDocument.isObject())
        {
            throw Wenku8::Error(Wenku8::ErrorCode::Io, "保存目录返回异常，请重新选择目录。");
        }
        return oDocument.object();
    }
    struct CallbackRegistration
    {
        jlong m_nToken;
        explicit CallbackRegistration(std::function<bool()> fnContinue)
            : m_nToken(oNextToken.fetch_add(1))
        {
            std::lock_guard<std::mutex> oLock(oCallbacksMutex);
            vCallbacks.emplace(m_nToken, std::move(fnContinue));
        }
        ~CallbackRegistration()
        {
            std::lock_guard<std::mutex> oLock(oCallbacksMutex);
            vCallbacks.erase(m_nToken);
        }
    };
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_lucky_1ji_wenku8_1reader_ExportDocuments_shouldContinue(JNIEnv*, jclass, jlong nToken)
{
    std::function<bool()> fnContinue;
    {
        std::lock_guard<std::mutex> oLock(oCallbacksMutex);
        const auto itCallback = vCallbacks.find(nToken);
        if (itCallback == vCallbacks.end())
        {
            return JNI_FALSE;
        }
        fnContinue = itCallback->second;
    }
    try
    {
        return !fnContinue || fnContinue() ? JNI_TRUE : JNI_FALSE;
    }
    catch (...)
    {
        return JNI_FALSE;
    }
}
namespace Wenku8::Reader
{
    QString privateDownloadDirectory()
    {
        const auto oResult = QAndroidJniObject::callStaticObjectMethod(
            pJavaClass, "privateDirectory", "(Landroid/content/Context;)Ljava/lang/String;",
            QtAndroid::androidContext().object());
        if (!jniOK() || !oResult.isValid() || oResult.toString().isEmpty())
        {
            throw Error(ErrorCode::Io, "无法访问应用的私有下载缓存目录。");
        }
        return oResult.toString();
    }
    QString exportDirectoryLabel(const QString& strLocation)
    {
        return strLocation.isEmpty() ? QString() : javaString("directoryLabel", strLocation);
    }
    bool exportDirectoryAvailable(const QString& strLocation)
    {
        if (strLocation.isEmpty())
        {
            return false;
        }
        const auto oLocation = QAndroidJniObject::fromString(strLocation);
        const bool bAvailable = QAndroidJniObject::callStaticMethod<jboolean>(
            pJavaClass, "directoryAvailable", "(Landroid/content/Context;Ljava/lang/String;)Z",
            QtAndroid::androidContext().object(), oLocation.object<jstring>());
        return jniOK() && bAvailable;
    }
    void chooseExportDirectory(QWidget* pParent,
                               std::function<void(const ExportDirectory&)> fnResult)
    {
        if (!fnResult || !qApp)
        {
            return;
        }
        const QPointer<QWidget> pGuard(pParent);
        const bool bHasParent = pParent != nullptr;
        const auto pDelivered = std::make_shared<std::atomic<bool>>(false);
        auto fnDeliver = [pGuard, bHasParent, fnResult, pDelivered](ExportDirectory oDirectory)
        {
            if (!pDelivered->exchange(true) && qApp)
            {
                QMetaObject::invokeMethod(qApp, [pGuard, bHasParent, fnResult, oDirectory]
                {
                    if (!bHasParent || pGuard)
                    {
                        fnResult(oDirectory);
                    }
                }, Qt::QueuedConnection);
            }
        };
        const auto oAction = QAndroidJniObject::fromString("android.intent.action.OPEN_DOCUMENT_TREE");
        QAndroidJniObject oIntent("android/content/Intent", "(Ljava/lang/String;)V",
                                  oAction.object<jstring>());
        oIntent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", jint(1 | 2 | 64 | 128));
        if (!jniOK() || !oIntent.isValid())
        {
            fnDeliver({{}, {}, "无法打开系统目录选择器。"});
            return;
        }
        const int nRequest = 0x6200 + (oNextRequest.fetch_add(1) & 0x0fff);
        QtAndroid::startActivity(oIntent, nRequest,
            [fnDeliver](int, int nResult, const QAndroidJniObject& oData)
            {
                if (nResult != -1)
                {
                    fnDeliver({});
                    return;
                }
                try
                {
                    const auto oResponse = QAndroidJniObject::callStaticObjectMethod(
                        pJavaClass, "persistDirectory",
                        "(Landroid/content/Context;Landroid/content/Intent;)Ljava/lang/String;",
                        QtAndroid::androidContext().object(), oData.object());
                    const auto oJson = parseResult(oResponse);
                    fnDeliver({oJson.value("location").toString(), oJson.value("label").toString(),
                               oJson.value("error").toString()});
                }
                catch (const std::exception&)
                {
                    fnDeliver({{}, {}, "无法取得目录授权，请重新选择保存目录。"});
                }
            });
        if (!jniOK())
        {
            fnDeliver({{}, {}, "无法打开系统目录选择器，请检查系统文件应用是否可用。"});
        }
    }
    QString publishExportDirectory(const QString& strStagedBookDirectory,
                                   const QString& strDestination,
                                   std::function<bool()> fnContinue)
    {
        CallbackRegistration oRegistration(std::move(fnContinue));
        const auto oSource = QAndroidJniObject::fromString(strStagedBookDirectory);
        const auto oDestination = QAndroidJniObject::fromString(strDestination);
        const auto oResponse = QAndroidJniObject::callStaticObjectMethod(
            pJavaClass, "publish",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;",
            QtAndroid::androidContext().object(), oSource.object<jstring>(),
            oDestination.object<jstring>(), oRegistration.m_nToken);
        const auto oJson = parseResult(oResponse);
        const QString strError = oJson.value("error").toString();
        if (!strError.isEmpty())
        {
            throw Error(oJson.value("cancelled").toBool() ? ErrorCode::Cancelled : ErrorCode::Io,
                        strError.toUtf8().toStdString());
        }
        const QString strLabel = oJson.value("label").toString();
        if (strLabel.isEmpty())
        {
            throw Error(ErrorCode::Io, "保存结果不完整，请检查所选目录。");
        }
        return strLabel;
    }
} // namespace Wenku8::Reader
