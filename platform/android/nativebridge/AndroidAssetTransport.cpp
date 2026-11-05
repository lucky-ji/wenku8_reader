// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QUrl>
#include <QApplication>
#include <QThread>
#include <jni.h>
#include <mutex>
#include <map>
#include <atomic>
namespace Wenku8::Reader
{
    namespace
    {
        struct Transfer
        {
            const TransferOptions& m_oOptions;
            std::chrono::steady_clock::time_point m_oDeadline;
            bool m_bCancelled = false;
        };
        std::mutex oTransfersMutex;
        std::map<jlong, Transfer*> vTransfers;
        std::atomic<jlong> oNextTransfer{1};
        class AndroidAssets final : public HttpTransport
        {
            void clearCookies() override
            {
            } // No cookie jar.
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oLimits) override
            {
                if (qApp && QThread::currentThread() == qApp->thread())
                    throw Error(ErrorCode::InvalidArgument,
                                "Call public image transport on a worker");
                QUrl oUrl(QString::fromStdString(oRequest.m_strUrl), QUrl::StrictMode);
                auto strHost = oUrl.host().toLower();
                if (oUrl.scheme() != "https" || !oUrl.isValid() || !oUrl.userInfo().isEmpty() ||
                    oUrl.hasFragment() || (oUrl.port() != -1 && oUrl.port() != 443) ||
                    (strHost != "img.wenku8.com" && strHost != "pic.wenku8.com" &&
                     strHost != "pic.777743.xyz"))
                    throw Error(ErrorCode::InvalidArgument, "Unsupported public image origin");
                if (oRequest.m_strMethod != "GET" || !oRequest.m_strBody.empty())
                    throw Error(ErrorCode::Unsupported, "Public images require GET");
                if (oLimits.m_oTimeout.count() <= 0 ||
                    oLimits.m_oTimeout > std::chrono::hours(24) ||
                    oLimits.m_oConnectTimeout.count() <= 0 || !oLimits.m_nMaxResponseBytes ||
                    oLimits.m_nMaxResponseBytes > 32ULL * 1024 * 1024)
                    throw Error(ErrorCode::InvalidArgument, "Invalid image transfer limits");
                for (const auto& oPair : oRequest.m_vHeaders)
                {
                    auto strName = QString::fromStdString(oPair.first).toLower();
                    if ((strName != "accept" && strName != "user-agent" && strName != "range" &&
                         strName != "if-range") ||
                        oPair.second.find_first_of("\r\n") != std::string::npos ||
                        oPair.second.find('\0') != std::string::npos)
                        throw Error(ErrorCode::Unsupported, "Unsupported image header");
                }
                Transfer oTransfer{oLimits, std::chrono::steady_clock::now() + oLimits.m_oTimeout};
                const auto nId = oNextTransfer.fetch_add(1);
                struct Registration
                {
                    jlong m_nId;
                    ~Registration()
                    {
                        std::lock_guard<std::mutex> oLock(oTransfersMutex);
                        vTransfers.erase(m_nId);
                    }
                } oRegistration{nId};
                {
                    std::lock_guard<std::mutex> oLock(oTransfersMutex);
                    vTransfers[nId] = &oTransfer;
                }
                QAndroidJniEnvironment oEnv;
                auto pStringClass = oEnv->FindClass("java/lang/String");
                auto pNames = oEnv->NewObjectArray(jsize(oRequest.m_vHeaders.size()), pStringClass,
                                                   nullptr),
                     pValues = oEnv->NewObjectArray(jsize(oRequest.m_vHeaders.size()), pStringClass,
                                                    nullptr);
                oEnv->DeleteLocalRef(pStringClass);
                for (jsize nI = 0; nI < jsize(oRequest.m_vHeaders.size()); ++nI)
                {
                    auto oName = QAndroidJniObject::fromString(
                        QString::fromStdString(oRequest.m_vHeaders[nI].first));
                    auto oValue = QAndroidJniObject::fromString(
                        QString::fromStdString(oRequest.m_vHeaders[nI].second));
                    oEnv->SetObjectArrayElement(pNames, nI, oName.object());
                    oEnv->SetObjectArrayElement(pValues, nI, oValue.object());
                }
                auto oAddress =
                    QAndroidJniObject::fromString(QString::fromStdString(oRequest.m_strUrl));
                auto oReply = QAndroidJniObject::callStaticObjectMethod(
                    "io/github/lucky_ji/wenku8_reader/PublicAssets", "get",
                    "(JLjava/lang/String;IIIZ[Ljava/lang/String;[Ljava/lang/String;)Lio/github/"
                    "lucky_ji/wenku8_reader/PublicAssets$Reply;",
                    nId, oAddress.object<jstring>(),
                    jint(std::min(oLimits.m_oConnectTimeout.count(), oLimits.m_oTimeout.count())),
                    jint(oLimits.m_oTimeout.count()), jint(oLimits.m_nMaxResponseBytes),
                    jboolean(oLimits.m_bFollowRedirects), pNames, pValues);
                oEnv->DeleteLocalRef(pNames);
                oEnv->DeleteLocalRef(pValues);
                if (oEnv->ExceptionCheck())
                {
                    oEnv->ExceptionClear();
                    throw Error(ErrorCode::Network, "Android image request unavailable");
                }
                if (!oReply.isValid())
                    throw Error(ErrorCode::Network, "Android image response unavailable");
                auto strError = oReply.getObjectField<jstring>("error").toString();
                if (oTransfer.m_bCancelled || strError == "cancel")
                    throw Error(ErrorCode::Cancelled, "Image request cancelled");
                if (!strError.isEmpty())
                    throw Error(strError == "size" ? ErrorCode::ResponseTooLarge
                                : (strError == "tls" || strError == "origin" ||
                                   strError == "session" || strError == "header" ||
                                   strError == "redirect")
                                    ? ErrorCode::Unsupported
                                    : ErrorCode::Network,
                                "Android image transfer failed");
                HttpResponse oResult;
                oResult.m_nStatus = oReply.getField<jint>("status");
                oResult.m_strEffectiveUrl =
                    oReply.getObjectField<jstring>("url").toString().toStdString();
                auto oBody = oReply.getObjectField("body", "[B");
                auto nCount = oEnv->GetArrayLength(oBody.object<jbyteArray>());
                if (std::size_t(nCount) > oLimits.m_nMaxResponseBytes)
                    throw Error(ErrorCode::ResponseTooLarge, "Image response exceeded limit");
                oResult.m_strBody.resize(nCount);
                if (nCount)
                    oEnv->GetByteArrayRegion(oBody.object<jbyteArray>(), 0, nCount,
                                             reinterpret_cast<jbyte*>(oResult.m_strBody.data()));
                auto oHeaderNames = oReply.getObjectField("headerNames", "[Ljava/lang/String;"),
                     oHeaderValues = oReply.getObjectField("headerValues", "[Ljava/lang/String;");
                const auto nHeaderCount = oEnv->GetArrayLength(oHeaderNames.object<jobjectArray>());
                for (jsize nI = 0; nI < nHeaderCount; ++nI)
                {
                    auto pName =
                             oEnv->GetObjectArrayElement(oHeaderNames.object<jobjectArray>(), nI),
                         pValue =
                             oEnv->GetObjectArrayElement(oHeaderValues.object<jobjectArray>(), nI);
                    oResult.m_vHeaders.emplace_back(
                        QAndroidJniObject(pName).toString().toStdString(),
                        QAndroidJniObject(pValue).toString().toStdString());
                    oEnv->DeleteLocalRef(pName);
                    oEnv->DeleteLocalRef(pValue);
                }
                return oResult;
            }
        };
    } // namespace
    std::shared_ptr<HttpTransport> makeAndroidAssetTransport()
    {
        return std::make_shared<AndroidAssets>();
    }
    bool continueAndroidAsset(jlong nId, jlong nReceived, jlong nTotal)
    {
        std::lock_guard<std::mutex> oLock(oTransfersMutex);
        auto oIt = vTransfers.find(nId);
        if (oIt == vTransfers.end())
            return false;
        auto& oTransfer = *oIt->second;
        try
        {
            oTransfer.m_bCancelled = oTransfer.m_oOptions.m_fnProgress &&
                                     !oTransfer.m_oOptions.m_fnProgress(nReceived, nTotal);
            return !oTransfer.m_bCancelled &&
                   std::chrono::steady_clock::now() < oTransfer.m_oDeadline;
        }
        catch (...)
        {
            oTransfer.m_bCancelled = true;
            return false;
        }
    }
} // namespace Wenku8::Reader
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_lucky_1ji_wenku8_1reader_PublicAssets_continueTransfer(JNIEnv*, jclass, jlong nId,
                                                                      jlong nReceived, jlong nTotal)
{
    return Wenku8::Reader::continueAndroidAsset(nId, nReceived, nTotal);
}
