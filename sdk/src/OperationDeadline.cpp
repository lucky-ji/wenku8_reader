// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Sdk.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <curl/curl.h>
namespace Wenku8
{
    namespace
    {
        class OperationDeadline final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;
            std::chrono::steady_clock::time_point m_oDeadline;

          public:
            OperationDeadline(std::shared_ptr<HttpTransport> pInner,
                              std::chrono::milliseconds oBudget)
                : m_pInner(std::move(pInner)), m_oDeadline(std::chrono::steady_clock::now())
            {
                if (!m_pInner || oBudget.count() <= 0 ||
                    oBudget > std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::time_point::max() - m_oDeadline))
                {
                    throw Error(ErrorCode::InvalidArgument, "Invalid operation deadline");
                }
                m_oDeadline += oBudget;
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return m_pInner->supportsSameOriginGetRedirects();
            }
            void clearCookies() override
            {
                m_pInner->clearCookies();
            }
            void importCookies(const std::string& strOrigin, const std::string& strCookies) override
            {
                m_pInner->importCookies(strOrigin, strCookies);
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                const auto oRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    m_oDeadline - std::chrono::steady_clock::now());
                if (oRemaining.count() <= 0)
                {
                    throw Error(ErrorCode::Network, "Operation deadline exceeded", 0, 0,
                                CURLE_OPERATION_TIMEDOUT);
                }
                auto oTransfer = oOptions;
                oTransfer.m_oTimeout = std::min(oTransfer.m_oTimeout, oRemaining);
                oTransfer.m_oConnectTimeout =
                    std::min(oTransfer.m_oConnectTimeout, oTransfer.m_oTimeout);
                const auto oDeadline = m_oDeadline;
                const auto fnProgress = oOptions.m_fnProgress;
                auto pUserCancelled = std::make_shared<std::atomic_bool>(false);
                oTransfer.m_fnProgress =
                    [oDeadline, fnProgress, pUserCancelled](auto oReceived, auto oTotal)
                {
                    try
                    {
                        if (fnProgress && !fnProgress(oReceived, oTotal))
                        {
                            pUserCancelled->store(true);
                            return false;
                        }
                    }
                    catch (...)
                    {
                        throw Error(ErrorCode::Network, "Operation progress callback failed");
                    }
                    return std::chrono::steady_clock::now() < oDeadline;
                };
                try
                {
                    if (!oTransfer.m_fnProgress(0, 0))
                        throw Error(ErrorCode::Cancelled, "Operation cancelled");
                    auto oResponse = m_pInner->perform(oRequest, oTransfer);
                    if (!oTransfer.m_fnProgress(oResponse.m_strBody.size(),
                                                oResponse.m_strBody.size()))
                        throw Error(ErrorCode::Cancelled, "Operation cancelled");
                    return oResponse;
                }
                catch (const Error& oError)
                {
                    if (oError.code() == ErrorCode::Cancelled && !pUserCancelled->load() &&
                        std::chrono::steady_clock::now() >= m_oDeadline)
                        throw Error(ErrorCode::Network, "Operation deadline exceeded", 0, 0,
                                    CURLE_OPERATION_TIMEDOUT);
                    throw;
                }
            }
        };
    } // namespace
    std::shared_ptr<HttpTransport> makeDeadlineTransport(std::shared_ptr<HttpTransport> pInner,
                                                         std::chrono::milliseconds oBudget)
    {
        return std::make_shared<OperationDeadline>(std::move(pInner), oBudget);
    }
} // namespace Wenku8
