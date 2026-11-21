// SPDX-License-Identifier: GPL-2.0-only
#include "RetryTransport.h"
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
        void validateRetryOptions(const RetryOptions& oRetry)
        {
            if (!oRetry.m_nMaxAttempts || oRetry.m_nMaxAttempts > 10 ||
                oRetry.m_oDelay.count() < 0 || oRetry.m_oDelay > std::chrono::seconds(60) ||
                oRetry.m_oMaxDelay.count() < 0 || oRetry.m_oMaxDelay > std::chrono::minutes(5) ||
                oRetry.m_oAttemptTimeout.count() < 0 ||
                std::any_of(oRetry.m_vHttpStatuses.begin(), oRetry.m_vHttpStatuses.end(),
                            [](long nS)
                            {
                                return !allowedStatus(nS);
                            }))
                throw Error(ErrorCode::InvalidArgument, "Invalid retry configuration");
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
        struct RateLimit
        {
            unsigned m_nRetries;
            std::chrono::milliseconds m_oWait;
        };

        class RetryingTransport final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;
            RetryOptions m_oRetry;
            std::shared_ptr<Detail::DownloadRequestGate> m_pGate;
            std::optional<RateLimit> m_oRateLimit;

            bool retryStatus(long nStatus) const
            {
                if (nStatus == 429 && m_oRateLimit)
                {
                    return m_oRateLimit->m_nRetries > 0;
                }
                return std::find(m_oRetry.m_vHttpStatuses.begin(), m_oRetry.m_vHttpStatuses.end(),
                                 nStatus) != m_oRetry.m_vHttpStatuses.end();
            }

          public:
            RetryingTransport(std::shared_ptr<HttpTransport> pInner, RetryOptions oOptions,
                              std::shared_ptr<Detail::DownloadRequestGate> pGate = {},
                              std::optional<RateLimit> oRateLimit = {})
                : m_pInner(std::move(pInner)), m_oRetry(std::move(oOptions)),
                  m_pGate(std::move(pGate)), m_oRateLimit(oRateLimit)
            {
                if (!m_pInner)
                {
                    throw Error(ErrorCode::InvalidArgument, "Missing retry transport");
                }
                validateRetryOptions(m_oRetry);
            }
            const std::shared_ptr<HttpTransport>& inner() const noexcept
            {
                return m_pInner;
            }
            const RetryOptions& options() const noexcept
            {
                return m_oRetry;
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
                const unsigned nAttempts = oRequest.m_strMethod == "GET" && oRequest.m_bAllowRetries
                                               ? m_oRetry.m_nMaxAttempts
                                               : 1;
                if (nAttempts == 1 && !m_pGate)
                {
                    return m_pInner->perform(oRequest, oOptions);
                }
                if (oOptions.m_oTimeout.count() <= 0)
                    throw Error(ErrorCode::InvalidArgument, "Invalid retry timeout");
                const auto oStarted = std::chrono::steady_clock::now();
                auto fnRemaining = [&]
                {
                    return oOptions.m_oTimeout -
                           std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - oStarted);
                };
                unsigned nRefused = 0, nRateLimitRetries = 0;
                for (unsigned nAttempt = 1;; ++nAttempt)
                {
                    cancellation(oOptions);
                    // Every attempted request, including a retry or redirect, observes the same
                    // pacing gate. Waiting before a transfer also spends its timeout budget.
                    if (m_pGate)
                    {
                        const auto oWait = m_pGate->m_oNext - std::chrono::steady_clock::now();
                        if (oWait >= fnRemaining())
                        {
                            throw Error(ErrorCode::Network, "Download request deadline exceeded", 0,
                                        0, CURLE_OPERATION_TIMEDOUT);
                        }
                        while (std::chrono::steady_clock::now() < m_pGate->m_oNext)
                        {
                            cancellation(oOptions);
                            const auto oLeft = std::chrono::ceil<std::chrono::milliseconds>(
                                m_pGate->m_oNext - std::chrono::steady_clock::now());
                            std::this_thread::sleep_for(
                                std::min(std::chrono::milliseconds(25), oLeft));
                        }
                        cancellation(oOptions);
                    }
                    auto oTransfer = oOptions;
                    oTransfer.m_oTimeout = fnRemaining();
                    if (oTransfer.m_oTimeout.count() <= 0)
                        throw Error(ErrorCode::Network, "Retry deadline exceeded", 0, 0,
                                    CURLE_OPERATION_TIMEDOUT);
                    if (nAttempts > 1 && m_oRetry.m_oAttemptTimeout.count() > 0)
                        oTransfer.m_oTimeout =
                            std::min(oTransfer.m_oTimeout, m_oRetry.m_oAttemptTimeout);
                    oTransfer.m_oConnectTimeout =
                        std::min(oOptions.m_oConnectTimeout, oTransfer.m_oTimeout);
                    std::optional<HttpResponse> oResponse;
                    std::exception_ptr oFailure;
                    int nNativeCode = 0;
                    bool bAgain = false;
                    bool bRefused = false;
                    long nStatus = 0;
                    if (m_pGate)
                    {
                        m_pGate->m_oNext = std::chrono::steady_clock::now() + m_pGate->m_oInterval;
                    }
                    if (nAttempts == 1)
                    {
                        return m_pInner->perform(oRequest, oTransfer);
                    }
                    try
                    {
                        oResponse = m_pInner->perform(oRequest, oTransfer);
                        nStatus = oResponse->m_nStatus;
                        bAgain = retryStatus(nStatus);
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
                        if (nStatus == 401)
                        {
                            return std::move(*oResponse);
                        }
                        bRefused = bChallenge || nStatus == 403;
                        if (bRefused)
                        {
                            if (!m_oRetry.m_bRetryRefusedResponses)
                            {
                                return std::move(*oResponse);
                            }
                            bAgain = true;
                        }
                        else if (nStatus >= 200 && nStatus < 300 && m_oRetry.m_fnAcceptResponse)
                        {
                            bAgain = !m_oRetry.m_fnAcceptResponse(*oResponse);
                        }
                    }
                    catch (const Error& oE)
                    {
                        nStatus = oE.httpStatus();
                        bRefused = nStatus == 403 ||
                                   oE.code() == ErrorCode::BrowserVerificationRequired ||
                                   oE.code() == ErrorCode::CaptchaRequired;
                        const bool bNetwork =
                            oE.code() == ErrorCode::Network &&
                            (transient(oE.transportCode()) ||
                             (oE.transportCode() == 0 && m_oRetry.m_bRetryBrowserNetworkErrors));
                        const bool bHttp = oE.code() == ErrorCode::Http && retryStatus(nStatus);
                        if (nStatus == 401 || oE.code() == ErrorCode::Cancelled ||
                            (bRefused ? !m_oRetry.m_bRetryRefusedResponses : (!bNetwork && !bHttp)))
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
                    if (!bAgain || nAttempt >= nAttempts || (bRefused && ++nRefused >= 3))
                        return fnFinish();
                    auto oDelay = m_oRetry.m_oDelay;
                    if (m_oRetry.m_bExponentialBackoff)
                        oDelay *= static_cast<int>(1U << (nAttempt - 1));
                    oDelay = std::min(oDelay, m_oRetry.m_oMaxDelay);
                    const auto oRequested = oResponse ? retryAfter(*oResponse) : std::nullopt;
                    if (nStatus == 429 && m_oRateLimit)
                    {
                        if (nRateLimitRetries >= m_oRateLimit->m_nRetries ||
                            (oRequested && *oRequested > m_oRateLimit->m_oWait))
                        {
                            return fnFinish();
                        }
                        ++nRateLimitRetries;
                        oDelay = oRequested.value_or(m_oRateLimit->m_oWait);
                    }
                    else if (oRequested)
                    {
                        if (*oRequested > m_oRetry.m_oMaxDelay)
                            return fnFinish();
                        oDelay = std::max(oDelay, *oRequested);
                    }
                    if (m_pGate)
                    {
                        oDelay = std::max(oDelay,
                                          std::chrono::ceil<std::chrono::milliseconds>(
                                              m_pGate->m_oNext - std::chrono::steady_clock::now()));
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
        std::shared_ptr<HttpTransport> rawTransport(std::shared_ptr<HttpTransport> pInner)
        {
            while (auto pConfigured = std::dynamic_pointer_cast<RetryingTransport>(pInner))
            {
                pInner = pConfigured->inner();
            }
            return pInner;
        }
    } // namespace
    std::shared_ptr<HttpTransport> makeRetryingTransport(std::shared_ptr<HttpTransport> pInner,
                                                         RetryOptions oOptions)
    {
        return std::make_shared<RetryingTransport>(std::move(pInner), std::move(oOptions));
    }
    RetryOptions defaultRetryOptions(RetryProfile eProfile)
    {
        RetryOptions oOptions;
        oOptions.m_oDelay = std::chrono::milliseconds(400);
        oOptions.m_vHttpStatuses = {408, 425, 429, 500, 502, 503, 504};
        oOptions.m_bRetryBrowserNetworkErrors = eProfile == RetryProfile::Browser;
        oOptions.m_bExponentialBackoff = true;
        oOptions.m_oAttemptTimeout = std::chrono::seconds(12);
        oOptions.m_oMaxDelay = std::chrono::seconds(15);
        return oOptions;
    }
    std::shared_ptr<HttpTransport> makeSdkTransport(std::shared_ptr<HttpTransport> pInner)
    {
        if (std::dynamic_pointer_cast<RetryingTransport>(pInner))
        {
            return pInner;
        }
        return makeSdkTransport(std::move(pInner), RetryPolicy{});
    }

    std::shared_ptr<HttpTransport> makeSdkTransport(std::shared_ptr<HttpTransport> pInner,
                                                    RetryPolicy oPolicy)
    {
        pInner = rawTransport(std::move(pInner));
        if (!pInner)
        {
            pInner = makeCurlTransport();
        }
        if (!oPolicy.m_bEnabled)
        {
            return pInner;
        }
        return makeRetryingTransport(std::move(pInner), std::move(oPolicy.m_oOptions));
    }

    void Detail::validateRetryPolicy(const RetryPolicy& oPolicy)
    {
        if (oPolicy.m_bEnabled)
        {
            validateRetryOptions(oPolicy.m_oOptions);
        }
    }

    RetryPolicy Detail::retryPolicyFor(const std::shared_ptr<HttpTransport>& pInner)
    {
        if (auto pConfigured = std::dynamic_pointer_cast<RetryingTransport>(pInner))
        {
            return {true, pConfigured->options()};
        }
        return {};
    }

    std::shared_ptr<HttpTransport> Detail::makeDownloadTransport(
        std::shared_ptr<HttpTransport> pInner, RetryPolicy oPolicy,
        std::shared_ptr<DownloadRequestGate> pGate, unsigned nRateLimitRetries,
        std::chrono::seconds oRateLimitWait)
    {
        if (!oPolicy.m_bEnabled)
        {
            oPolicy.m_oOptions = {};
            oPolicy.m_oOptions.m_nMaxAttempts = 1;
        }
        // Install pacing inside the only retry envelope, before each actual request.
        return std::make_shared<RetryingTransport>(rawTransport(std::move(pInner)),
                                                   std::move(oPolicy.m_oOptions), std::move(pGate),
                                                   RateLimit{nRateLimitRetries, oRateLimitWait});
    }
} // namespace Wenku8
