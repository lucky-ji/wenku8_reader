// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
#include "NetworkRetry.h"
#include <algorithm>
#include <atomic>
namespace Wenku8::Reader
{
#ifdef WENKU8_ANDROID_ASSET_TLS
    std::shared_ptr<HttpTransport> makeAndroidAssetTransport();
#endif
    std::shared_ptr<HttpTransport> makeClientAssetTransport()
    {
#ifdef WENKU8_ANDROID_ASSET_TLS
        return makeAndroidAssetTransport();
#else
        return makeCurlTransport();
#endif
    }
    RetryOptions clientAssetRetryOptions(bool bRateLimit)
    {
#ifdef WENKU8_ANDROID_ASSET_TLS
        return clientRetryOptions(true, bRateLimit);
#else
        return clientRetryOptions(false, bRateLimit);
#endif
    }
    namespace
    {
        class OperationDeadline final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;
            std::chrono::steady_clock::time_point m_oDeadline;

          public:
            OperationDeadline(std::shared_ptr<HttpTransport> pInner,
                              std::chrono::milliseconds oBudget)
                : m_pInner(std::move(pInner)),
                  m_oDeadline(std::chrono::steady_clock::now() + oBudget)
            {
                if (!m_pInner || oBudget.count() <= 0)
                    throw Error(ErrorCode::InvalidArgument, "Invalid operation deadline");
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
                    throw Error(ErrorCode::Network, "Operation deadline exceeded", 0, 0, 28);
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
                    if (fnProgress && !fnProgress(oReceived, oTotal))
                    {
                        pUserCancelled->store(true);
                        return false;
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
                        throw Error(ErrorCode::Network, "Operation deadline exceeded", 0, 0, 28);
                    throw;
                }
            }
        };
    } // namespace
    std::shared_ptr<HttpTransport> makeClientDeadlineTransport(
        std::shared_ptr<HttpTransport> pInner, std::chrono::milliseconds oBudget)
    {
        return std::make_shared<OperationDeadline>(std::move(pInner), oBudget);
    }

} // namespace Wenku8::Reader

