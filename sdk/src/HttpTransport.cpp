// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Sdk.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <curl/curl.h>
#include <algorithm>
#include <climits>
#include <limits>
#include <mutex>

namespace Wenku8
{
    Error::Error(ErrorCode eCode, const std::string& strMessage, long nStatus, int nApiCode,
                 int nTransportCode)
        : std::runtime_error(strMessage), m_eCode(eCode), m_nStatus(nStatus), m_nApiCode(nApiCode),
          m_nTransportCode(nTransportCode)
    {
    }

    namespace
    {
        std::string encode(const std::string& strValue)
        {
            static constexpr char vHex[] = "0123456789ABCDEF";
            std::string strOut;
            for (unsigned char nC : strValue)
            {
                if ((nC >= 'a' && nC <= 'z') || (nC >= 'A' && nC <= 'Z') ||
                    (nC >= '0' && nC <= '9') || nC == '-' || nC == '_' || nC == '.' || nC == '*')
                    strOut += static_cast<char>(nC);
                else if (nC == ' ')
                    strOut += '+';
                else
                {
                    strOut += '%';
                    strOut += vHex[nC >> 4];
                    strOut += vHex[nC & 15];
                }
            }
            return strOut;
        }
        struct GlobalCurl
        {
            GlobalCurl()
            {
                if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                    throw Error(ErrorCode::Network, "curl_global_init failed");
            }
            ~GlobalCurl()
            {
                curl_global_cleanup();
            }
        };
        struct Transfer
        {
            const TransferOptions& m_oOptions;
            std::string m_strBytes;
            bool m_bOversized = false, m_bCancelled = false, m_bCallbackFailed = false;
            Fields m_vHeaders;
        };
        std::size_t receiveHeader(char* pData, std::size_t nSize, std::size_t nCount,
                                  void* pContext) noexcept
        {
            auto& oT = *static_cast<Transfer*>(pContext);
            if (nSize && nCount > (std::numeric_limits<std::size_t>::max)() / nSize)
                return 0;
            const auto nBytes = nSize * nCount;
            try
            {
                std::string strLine(pData, nBytes);
                if (strLine.rfind("HTTP/", 0) == 0)
                    oT.m_vHeaders.clear();
                else if (auto nColon = strLine.find(':'); nColon != std::string::npos)
                {
                    auto nBegin = strLine.find_first_not_of(" \t", nColon + 1);
                    auto nEnd = strLine.find_last_not_of("\r\n \t");
                    oT.m_vHeaders.emplace_back(strLine.substr(0, nColon),
                                               nBegin == std::string::npos || nEnd < nBegin
                                                   ? ""
                                                   : strLine.substr(nBegin, nEnd - nBegin + 1));
                }
                return nBytes;
            }
            catch (...)
            {
                oT.m_bCallbackFailed = true;
                return 0;
            }
        }
        std::size_t receive(char* pData, std::size_t nSize, std::size_t nCount,
                            void* pContext) noexcept
        {
            auto& oT = *static_cast<Transfer*>(pContext);
            if (nSize && nCount > std::numeric_limits<std::size_t>::max() / nSize)
            {
                oT.m_bOversized = true;
                return 0;
            }
            const auto nBytes = nSize * nCount;
            if (nBytes > oT.m_oOptions.m_nMaxResponseBytes - oT.m_strBytes.size())
            {
                oT.m_bOversized = true;
                return 0;
            }
            try
            {
                oT.m_strBytes.append(pData, nBytes);
                return nBytes;
            }
            catch (...)
            {
                oT.m_bCallbackFailed = true;
                return 0;
            }
        }
        int progress(void* pContext, curl_off_t nTotal, curl_off_t nNow, curl_off_t,
                     curl_off_t) noexcept
        {
            auto& oT = *static_cast<Transfer*>(pContext);
            try
            {
                if (oT.m_oOptions.m_fnProgress &&
                    !oT.m_oOptions.m_fnProgress(
                        static_cast<std::uint64_t>(std::max<curl_off_t>(nNow, 0)),
                        static_cast<std::uint64_t>(std::max<curl_off_t>(nTotal, 0))))
                {
                    oT.m_bCancelled = true;
                    return 1;
                }
                return 0;
            }
            catch (...)
            {
                oT.m_bCallbackFailed = true;
                return 1;
            }
        }
        class CurlTransport final : public HttpTransport
        {
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> m_pHandle{nullptr,
                                                                          curl_easy_cleanup};
            std::string m_strCaBundle;
            std::mutex m_oMutex;
            template <class T> void set(CURLoption eKey, T oValue)
            {
                if (curl_easy_setopt(m_pHandle.get(), eKey, oValue) != CURLE_OK)
                    throw Error(ErrorCode::Network, "curl option unavailable");
            }

          public:
            explicit CurlTransport(std::string strCa) : m_strCaBundle(std::move(strCa))
            {
                static GlobalCurl oGlobal;
                m_pHandle.reset(curl_easy_init());
                if (!m_pHandle)
                    throw Error(ErrorCode::Network, "curl_easy_init failed");
                set(CURLOPT_COOKIEFILE, "");
            }
            void clearCookies() override
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                set(CURLOPT_COOKIELIST, "ALL");
            }
            void importCookies(const std::string& strOrigin, const std::string& strCookies) override
            {
                if (strCookies.empty() || strCookies.size() > 32768 ||
                    strCookies.find_first_of("\r\n\t") != std::string::npos ||
                    strCookies.find('\0') != std::string::npos)
                    throw Error(ErrorCode::InvalidArgument, "Invalid session cookies");
                std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> pUrl(curl_url(),
                                                                         curl_url_cleanup);
                if (!pUrl ||
                    curl_url_set(pUrl.get(), CURLUPART_URL, strOrigin.c_str(), 0) != CURLUE_OK)
                    throw Error(ErrorCode::InvalidArgument, "Invalid cookie origin");
                char* pHost = nullptr;
                char* pScheme = nullptr;
                if (curl_url_get(pUrl.get(), CURLUPART_HOST, &pHost, 0) != CURLUE_OK)
                    throw Error(ErrorCode::InvalidArgument, "Missing cookie host");
                std::unique_ptr<char, decltype(&curl_free)> pHostHolder(pHost, curl_free);
                if (curl_url_get(pUrl.get(), CURLUPART_SCHEME, &pScheme, 0) != CURLUE_OK)
                    throw Error(ErrorCode::InvalidArgument, "Missing cookie scheme");
                std::unique_ptr<char, decltype(&curl_free)> pSchemeHolder(pScheme, curl_free);
                const std::string strPrefix = std::string(pHost) + "\tFALSE\t/\t" +
                                              (std::string(pScheme) == "https" ? "TRUE" : "FALSE") +
                                              "\t0\t";
                std::vector<std::string> vLines;
                for (std::size_t nAt = 0; nAt < strCookies.size();)
                {
                    auto nEnd = strCookies.find(';', nAt);
                    if (nEnd == std::string::npos)
                        nEnd = strCookies.size();
                    auto strPair = strCookies.substr(nAt, nEnd - nAt);
                    auto nFirst = strPair.find_first_not_of(' ');
                    if (nFirst != std::string::npos)
                    {
                        strPair.erase(0, nFirst);
                        auto nEqual = strPair.find('=');
                        if (nEqual == 0 || nEqual == std::string::npos ||
                            strPair.substr(0, nEqual).find_first_of(" ()<>@,;:\\/[?]={}") !=
                                std::string::npos)
                            throw Error(ErrorCode::InvalidArgument, "Invalid cookie pair");
                        vLines.push_back(strPrefix + strPair.substr(0, nEqual) + "\t" +
                                         strPair.substr(nEqual + 1));
                    }
                    nAt = nEnd + 1;
                }
                if (vLines.empty())
                    throw Error(ErrorCode::InvalidArgument, "No session cookies");
                std::lock_guard<std::mutex> oLock(m_oMutex);
                for (const auto& strLine : vLines)
                    set(CURLOPT_COOKIELIST, strLine.c_str());
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                if ((oRequest.m_strUrl.rfind("https://", 0) != 0 &&
                     oRequest.m_strUrl.rfind("http://", 0) != 0) ||
                    oRequest.m_strUrl.find('\0') != std::string::npos ||
                    (oRequest.m_strMethod != "GET" && oRequest.m_strMethod != "POST") ||
                    oOptions.m_oConnectTimeout.count() <= 0 || oOptions.m_oTimeout.count() <= 0 ||
                    !oOptions.m_nMaxResponseBytes || oOptions.m_oTimeout.count() > LONG_MAX ||
                    oOptions.m_oConnectTimeout.count() > LONG_MAX)
                    throw Error(ErrorCode::InvalidArgument,
                                "Invalid HTTP request or transfer limits");
                std::lock_guard<std::mutex> oLock(m_oMutex);
                auto* pH = m_pHandle.get();
                curl_easy_reset(pH); // Retains cookies and connection pool.
                char vErrorBuffer[CURL_ERROR_SIZE]{};
                set(CURLOPT_ERRORBUFFER, vErrorBuffer);
                Transfer oTransfer{oOptions};
                std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> pHeaders(
                    nullptr, curl_slist_free_all);
                for (const auto& oHeader : oRequest.m_vHeaders)
                {
                    if (oHeader.first.empty() ||
                        oHeader.first.find_first_of(":\r\n") != std::string::npos ||
                        oHeader.second.find_first_of("\r\n") != std::string::npos ||
                        oHeader.first.find('\0') != std::string::npos ||
                        oHeader.second.find('\0') != std::string::npos)
                        throw Error(ErrorCode::InvalidArgument, "Invalid HTTP header");
                    auto pNext = curl_slist_append(pHeaders.get(),
                                                   (oHeader.first + ": " + oHeader.second).c_str());
                    if (!pNext)
                        throw Error(ErrorCode::Network, "Cannot allocate HTTP headers");
                    pHeaders.release();
                    pHeaders.reset(pNext);
                }
                set(CURLOPT_URL, oRequest.m_strUrl.c_str());
                set(CURLOPT_PROTOCOLS_STR, "http,https");
                // A redirect must never downgrade an HTTPS request to HTTP.
                set(CURLOPT_REDIR_PROTOCOLS_STR,
                    oRequest.m_strUrl.rfind("https://", 0) == 0 ? "https" : "http,https");
                set(CURLOPT_FOLLOWLOCATION, oOptions.m_bFollowRedirects ? 1L : 0L);
                set(CURLOPT_MAXREDIRS, 5L);
                set(CURLOPT_SSL_VERIFYPEER, 1L);
                set(CURLOPT_SSL_VERIFYHOST, 2L);
                if (!m_strCaBundle.empty())
                    set(CURLOPT_CAINFO, m_strCaBundle.c_str());
                set(CURLOPT_COOKIEFILE, "");
                set(CURLOPT_NOSIGNAL, 1L);
                set(CURLOPT_CONNECTTIMEOUT_MS,
                    static_cast<long>(oOptions.m_oConnectTimeout.count()));
                set(CURLOPT_TIMEOUT_MS, static_cast<long>(oOptions.m_oTimeout.count()));
                set(CURLOPT_USERAGENT, "Wenku8CppSdk/0.1");
                set(CURLOPT_ACCEPT_ENCODING, "");
                set(CURLOPT_HTTPHEADER, pHeaders.get());
                set(CURLOPT_WRITEFUNCTION, &receive);
                set(CURLOPT_WRITEDATA, &oTransfer);
                set(CURLOPT_HEADERFUNCTION, &receiveHeader);
                set(CURLOPT_HEADERDATA, &oTransfer);
                set(CURLOPT_NOPROGRESS, 0L);
                set(CURLOPT_XFERINFOFUNCTION, &progress);
                set(CURLOPT_XFERINFODATA, &oTransfer);
                if (oRequest.m_strMethod == "POST")
                {
                    set(CURLOPT_POST, 1L);
                    set(CURLOPT_POSTFIELDS, oRequest.m_strBody.data());
                    set(CURLOPT_POSTFIELDSIZE_LARGE,
                        static_cast<curl_off_t>(oRequest.m_strBody.size()));
                }
                const auto eCode = curl_easy_perform(pH);
                if (oTransfer.m_bCancelled)
                    throw Error(ErrorCode::Cancelled, "Transfer cancelled");
                if (oTransfer.m_bOversized)
                    throw Error(ErrorCode::ResponseTooLarge, "Response exceeds configured limit");
                if (oTransfer.m_bCallbackFailed)
                    throw Error(ErrorCode::Network, "Transfer callback failed");
                if (eCode != CURLE_OK)
                    throw Error(ErrorCode::Network,
                                vErrorBuffer[0] ? vErrorBuffer : curl_easy_strerror(eCode), 0, 0,
                                static_cast<int>(eCode));
                HttpResponse oResponse;
                curl_easy_getinfo(pH, CURLINFO_RESPONSE_CODE, &oResponse.m_nStatus);
                char* pUrl = nullptr;
                curl_easy_getinfo(pH, CURLINFO_EFFECTIVE_URL, &pUrl);
                if (pUrl)
                    oResponse.m_strEffectiveUrl = pUrl;
                oResponse.m_strBody = std::move(oTransfer.m_strBytes);
                oResponse.m_vHeaders = std::move(oTransfer.m_vHeaders);
                return oResponse;
            }
        };
    } // namespace
    std::string formEncode(const Fields& vFields)
    {
        std::string strResult;
        for (const auto& oField : vFields)
        {
            if (!strResult.empty())
                strResult += '&';
            strResult += encode(oField.first) + '=' + encode(oField.second);
        }
        return strResult;
    }
    std::shared_ptr<HttpTransport> makeCurlTransport(const std::string& strCa)
    {
        return std::make_shared<CurlTransport>(strCa);
    }
} // namespace Wenku8
