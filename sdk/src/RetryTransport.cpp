// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Sdk.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <curl/curl.h>
#include <algorithm>
#include <thread>
#include <ctime>
#include <limits>
#include <cctype>
namespace Wenku8
{
    namespace
    {
        bool transient(int nCode)
        {
            switch (nCode)
            {
            case CURLE_COULDNT_RESOLVE_PROXY:
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_PARTIAL_FILE:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_GOT_NOTHING:
            case CURLE_SEND_ERROR:
            case CURLE_RECV_ERROR:
                return true;
            default:
                return false;
            }
        }
        bool allowedStatus(long nCode)
        {
            return nCode == 404 || nCode == 408 || nCode == 425 || nCode == 429 || nCode == 500 ||
                   nCode == 502 || nCode == 503 || nCode == 504;
        }
        void cancellation(const TransferOptions& oOptions)
        {
            if (!oOptions.m_fnProgress)
                return;
            bool bProceed;
            try
            {
                bProceed = oOptions.m_fnProgress(0, 0);
            }
            catch (...)
            {
                throw Error(ErrorCode::Network, "Retry progress callback failed");
            }
            if (!bProceed)
                throw Error(ErrorCode::Cancelled, "Retry cancelled");
        }
        std::optional<std::chrono::milliseconds> retryAfter(const HttpResponse& oResponse)
        {
            for (const auto& oH : oResponse.m_vHeaders)
            {
                std::string strName = oH.first;
                std::transform(strName.begin(), strName.end(), strName.begin(),
                               [](unsigned char nC)
                               {
                                   return char(std::tolower(nC));
                               });
                if (strName != "retry-after")
                    continue;
                auto strValue = oH.second;
                auto nBegin = strValue.find_first_not_of(" \t\r\n");
                if (nBegin == std::string::npos)
                    continue;
                strValue =
                    strValue.substr(nBegin, strValue.find_last_not_of(" \t\r\n") - nBegin + 1);
                if (std::all_of(strValue.begin(), strValue.end(),
                                [](unsigned char nC)
                                {
                                    return nC >= '0' && nC <= '9';
                                }))
                {
                    unsigned long long nSeconds = 0;
                    for (char chC : strValue)
                    {
                        if (nSeconds > 86400)
                            return std::chrono::hours(24);
                        nSeconds = nSeconds * 10 + unsigned(chC - '0');
                    }
                    return std::chrono::milliseconds(std::min(nSeconds, 86400ULL) * 1000);
                }
                auto nDate = curl_getdate(strValue.c_str(), nullptr);
                if (nDate >= 0)
                {
                    auto fSeconds =
                        std::clamp<double>(std::difftime(nDate, std::time(nullptr)), 0, 86400);
                    return std::chrono::milliseconds(static_cast<long long>(fSeconds * 1000));
                }
            }
            return {};
        }
        class RetryingTransport final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;
            RetryOptions m_oRetry;

          public:
            RetryingTransport(std::shared_ptr<HttpTransport> pInner, RetryOptions oOptions)
                : m_pInner(std::move(pInner)), m_oRetry(std::move(oOptions))
            {
                if (!m_pInner || !m_oRetry.m_nMaxAttempts || m_oRetry.m_nMaxAttempts > 10 ||
                    m_oRetry.m_oDelay.count() < 0 || m_oRetry.m_oDelay > std::chrono::seconds(60) ||
                    m_oRetry.m_oMaxDelay.count() < 0 ||
                    m_oRetry.m_oMaxDelay > std::chrono::minutes(5) ||
                    m_oRetry.m_oAttemptTimeout.count() < 0 ||
                    std::any_of(m_oRetry.m_vHttpStatuses.begin(), m_oRetry.m_vHttpStatuses.end(),
                                [](long nS)
                                {
                                    return !allowedStatus(nS);
                                }))
                    throw Error(ErrorCode::InvalidArgument, "Invalid retry configuration");
            }
            void clearCookies() override
            {
                m_pInner->clearCookies();
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return m_pInner->supportsSameOriginGetRedirects();
            }
            void importCookies(const std::string& strOrigin, const std::string& strCookies) override
            {
                m_pInner->importCookies(strOrigin, strCookies);
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                const auto nAttempts = m_oRetry.m_bRetryRefusedResponses
                                           ? (std::min)(m_oRetry.m_nMaxAttempts, 3U)
                                           : m_oRetry.m_nMaxAttempts;
                if (oRequest.m_strMethod != "GET" || !oRequest.m_bAllowRetries ||
                    nAttempts == 1)
                    return m_pInner->perform(oRequest, oOptions);
                if (oOptions.m_oTimeout.count() <= 0)
                    throw Error(ErrorCode::InvalidArgument, "Invalid retry timeout");
                const auto oStarted = std::chrono::steady_clock::now();
                auto fnRemaining = [&]
                {
                    return oOptions.m_oTimeout -
                           std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - oStarted);
                };
                for (unsigned nAttempt = 1;; ++nAttempt)
                {
                    cancellation(oOptions);
                    auto oTransfer = oOptions;
                    oTransfer.m_oTimeout = fnRemaining();
                    if (oTransfer.m_oTimeout.count() <= 0)
                        throw Error(ErrorCode::Network, "Retry deadline exceeded", 0, 0,
                                    CURLE_OPERATION_TIMEDOUT);
                    if (m_oRetry.m_oAttemptTimeout.count() > 0)
                        oTransfer.m_oTimeout =
                            std::min(oTransfer.m_oTimeout, m_oRetry.m_oAttemptTimeout);
                    oTransfer.m_oConnectTimeout =
                        std::min(oOptions.m_oConnectTimeout, oTransfer.m_oTimeout);
                    std::optional<HttpResponse> oResponse;
                    std::exception_ptr oFailure;
                    int nNativeCode = 0;
                    bool bAgain = false;
                    try
                    {
                        oResponse = m_pInner->perform(oRequest, oTransfer);
                        bAgain = std::find(m_oRetry.m_vHttpStatuses.begin(),
                                           m_oRetry.m_vHttpStatuses.end(),
                                           oResponse->m_nStatus) != m_oRetry.m_vHttpStatuses.end();
                        std::string strSample = oResponse->m_strBody.substr(0, 8192);
                        std::transform(strSample.begin(), strSample.end(), strSample.begin(),
                                       [](unsigned char nC)
                                       {
                                           return char(std::tolower(nC));
                                       });
                        // Ordinary successful pages also contain Cloudflare's
                        // background JSD script. Match the website backend's
                        // distinction rather than retrying those valid pages.
                        const bool bPlatform = strSample.find("challenge-platform") != std::string::npos;
                        const bool bBackgroundDetection = strSample.find("/scripts/jsd/") != std::string::npos;
                        const bool bChallenge =
                            (bPlatform && !bBackgroundDetection) ||
                            strSample.find("__cf_chl_opt") != std::string::npos ||
                            strSample.find("cf-chl-") != std::string::npos ||
                            strSample.find("<title>just a moment") != std::string::npos ||
                            strSample.find("you have been blocked") != std::string::npos;
                        if (oResponse->m_nStatus == 401)
                        {
                            return std::move(*oResponse);
                        }
                        if (bChallenge)
                        {
                            if (!m_oRetry.m_bRetryRefusedResponses)
                            {
                                return std::move(*oResponse);
                            }
                            bAgain = true;
                        }
                        else if (oResponse->m_nStatus == 403 && m_oRetry.m_bRetryRefusedResponses)
                        {
                            bAgain = true;
                        }
                        else if (oResponse->m_nStatus >= 200 && oResponse->m_nStatus < 300 &&
                            m_oRetry.m_fnAcceptResponse)
                        {
                            bAgain = !m_oRetry.m_fnAcceptResponse(*oResponse);
                        }
                    }
                    catch (const Error& oE)
                    {
                        const bool bRefused = oE.httpStatus() == 403 ||
                                              oE.code() == ErrorCode::BrowserVerificationRequired ||
                                              oE.code() == ErrorCode::CaptchaRequired;
                        if (oE.httpStatus() == 401 ||
                            (oE.code() == ErrorCode::Authentication && oE.httpStatus() != 403) ||
                            oE.code() == ErrorCode::Cancelled ||
                            (bRefused && !m_oRetry.m_bRetryRefusedResponses) ||
                            (!bRefused &&
                             (oE.code() != ErrorCode::Network ||
                              (!transient(oE.transportCode()) &&
                               !(oE.transportCode() == 0 && m_oRetry.m_bRetryBrowserNetworkErrors)))))
                        {
                            throw;
                        }
                        oFailure = std::current_exception();
                        nNativeCode = oE.transportCode();
                        bAgain = true;
                    }
                    auto fnFinish = [&]() -> HttpResponse
                    {
                        if (oFailure)
                            std::rethrow_exception(oFailure);
                        return std::move(*oResponse);
                    };
                    if (!bAgain || nAttempt >= nAttempts)
                        return fnFinish();
                    auto oDelay = m_oRetry.m_oDelay;
                    if (m_oRetry.m_bExponentialBackoff)
                        oDelay *= static_cast<int>(1U << (nAttempt - 1));
                    oDelay = std::min(oDelay, m_oRetry.m_oMaxDelay);
                    if (oResponse)
                    {
                        if (auto oRequested = retryAfter(*oResponse))
                        {
                            if (*oRequested > m_oRetry.m_oMaxDelay)
                                return fnFinish();
                            oDelay = std::max(oDelay, *oRequested);
                        }
                    }
                    if (fnRemaining() <= oDelay)
                        return fnFinish();
                    cancellation(oOptions);
                    if (m_oRetry.m_fnOnRetry)
                        m_oRetry.m_fnOnRetry(nAttempt + 1, nNativeCode);
                    const auto oUntil = std::chrono::steady_clock::now() + oDelay;
                    while (std::chrono::steady_clock::now() < oUntil)
                    {
                        cancellation(oOptions);
                        auto oLeft = std::chrono::duration_cast<std::chrono::milliseconds>(
                            oUntil - std::chrono::steady_clock::now());
                        if (fnRemaining().count() <= 0)
                            return fnFinish();
                        std::this_thread::sleep_for(
                            std::min({std::chrono::milliseconds(25), oLeft, fnRemaining()}));
                    }
                    cancellation(oOptions);
                }
            }
        };
    } // namespace
    std::shared_ptr<HttpTransport> makeRetryingTransport(std::shared_ptr<HttpTransport> pInner,
                                                         RetryOptions oOptions)
    {
        return std::make_shared<RetryingTransport>(std::move(pInner), std::move(oOptions));
    }
} // namespace Wenku8
