// SPDX-License-Identifier: GPL-2.0-only
#include "MobileBrowserProtocol.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
namespace Wenku8::Reader::MobileBrowserProtocol
{
    bool sameOrigin(const QUrl& oA, const QUrl& oB)
    {
        return oA.isValid() && oB.isValid() && oA.scheme() == oB.scheme() &&
               oA.host() == oB.host() && oA.port(443) == oB.port(443);
    }
    void validateOrigin(const QUrl& oOrigin)
    {
        if (!oOrigin.isValid() || oOrigin.scheme() != "https" || oOrigin.host().isEmpty() ||
            !oOrigin.userInfo().isEmpty() || oOrigin.hasQuery() || oOrigin.hasFragment() ||
            (!oOrigin.path().isEmpty() && oOrigin.path() != "/"))
            throw Error(ErrorCode::InvalidArgument, "Mobile browser requires an HTTPS origin");
    }
    QJsonObject requestConfig(const QUrl& oOrigin, const HttpRequest& oRequest,
                              const TransferOptions& oLimits)
    {
        validateOrigin(oOrigin);
        QUrl oUrl(QString::fromStdString(oRequest.m_strUrl), QUrl::StrictMode);
        if (!sameOrigin(oOrigin, oUrl) || !oUrl.userInfo().isEmpty() || oUrl.hasFragment() ||
            oRequest.m_strUrl.find('\0') != std::string::npos)
            throw Error(ErrorCode::InvalidArgument, "Mobile browser requires a same-origin URL");
        if (oRequest.m_strMethod != "GET" || !oRequest.m_strBody.empty())
            throw Error(ErrorCode::Unsupported, "Sign in through the browser view");
        if (oLimits.m_oTimeout.count() <= 0 || oLimits.m_oTimeout > std::chrono::hours(24) ||
            !oLimits.m_nMaxResponseBytes || oLimits.m_nMaxResponseBytes > 32ULL * 1024 * 1024)
            throw Error(ErrorCode::InvalidArgument, "Invalid mobile browser transfer limits");
        QJsonObject oHeaders;
        for (const auto& oPair : oRequest.m_vHeaders)
        {
            auto strName = QString::fromStdString(oPair.first).toLower();
            if ((strName != "accept" && strName != "user-agent" && strName != "referer") ||
                oPair.second.find_first_of("\r\n") != std::string::npos ||
                oPair.second.find('\0') != std::string::npos)
                throw Error(ErrorCode::Unsupported,
                            "Browser-managed or unsupported request header");
            if (strName == "accept")
                oHeaders.insert(strName, QString::fromStdString(oPair.second));
        }
        return {{"url", oUrl.toString(QUrl::FullyEncoded)},
                {"headers", oHeaders},
                {"limit", double(oLimits.m_nMaxResponseBytes)},
                {"follow", oLimits.m_bFollowRedirects}};
    }
    HttpResponse response(const QUrl& oOrigin, const QByteArray& baBytes, std::size_t nLimit)
    {
        if (quint64(baBytes.size()) > quint64(nLimit) * 4 / 3 + 131072)
            throw Error(ErrorCode::ResponseTooLarge, "Mobile bridge response exceeded limit");
        QJsonParseError oParse;
        auto oDoc = QJsonDocument::fromJson(baBytes, &oParse);
        auto oData = oDoc.object();
        if (oParse.error != QJsonParseError::NoError || !oData.value("done").toBool())
            throw Error(ErrorCode::Network, "Invalid mobile browser reply");
        auto strError = oData.value("error").toString();
        if (!strError.isEmpty())
            throw Error(strError == "size"       ? ErrorCode::ResponseTooLarge
                        : strError == "redirect" ? ErrorCode::Unsupported
                        : strError == "cancel"   ? ErrorCode::Cancelled
                                                 : ErrorCode::Network,
                        "Mobile browser request failed");
        HttpResponse oResult;
        oResult.m_nStatus = oData.value("status").toInt();
        oResult.m_strEffectiveUrl = oData.value("url").toString().toStdString();
        QUrl oFinalUrl(QString::fromStdString(oResult.m_strEffectiveUrl), QUrl::StrictMode);
        if (oResult.m_nStatus < 100 || oResult.m_nStatus > 599 || !sameOrigin(oOrigin, oFinalUrl) ||
            !oFinalUrl.userInfo().isEmpty())
            throw Error(ErrorCode::Network,
                        "Mobile browser returned an invalid response origin/status");
        auto oEncoded = oData.value("body");
        if (!oEncoded.isString() ||
            quint64(oEncoded.toString().size()) > 4 * ((quint64(nLimit) + 2) / 3))
            throw Error(ErrorCode::ResponseTooLarge, "Invalid mobile browser body length");
        auto oDecoded = QByteArray::fromBase64Encoding(oEncoded.toString().toLatin1(),
                                                       QByteArray::AbortOnBase64DecodingErrors);
        if (!oDecoded || std::size_t(oDecoded.decoded.size()) > nLimit)
            throw Error(ErrorCode::ResponseTooLarge, "Invalid mobile browser body");
        oResult.m_strBody = oDecoded.decoded.toStdString();
        for (const auto& oValue : oData.value("headers").toArray())
        {
            auto oPair = oValue.toArray();
            if (oPair.size() != 2)
                throw Error(ErrorCode::Network, "Invalid browser response headers");
            auto strName = oPair[0].toString();
            auto strText = oPair[1].toString();
            if (strName.contains('\r') || strName.contains('\n') || strText.contains('\r') ||
                strText.contains('\n'))
                throw Error(ErrorCode::Network, "Invalid browser response header bytes");
            if (strName.compare("set-cookie", Qt::CaseInsensitive) != 0)
                oResult.m_vHeaders.emplace_back(strName.toStdString(), strText.toStdString());
        }
        return oResult;
    }
} // namespace Wenku8::Reader::MobileBrowserProtocol
