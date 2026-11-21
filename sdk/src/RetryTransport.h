// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"

namespace Wenku8::Detail
{
    // A single downloader serializes content and asset requests through this gate.
    struct DownloadRequestGate
    {
        std::chrono::milliseconds m_oInterval;
        std::chrono::steady_clock::time_point m_oNext{};
    };

    void validateRetryPolicy(const RetryPolicy&);
    RetryPolicy retryPolicyFor(const std::shared_ptr<HttpTransport>&);
    std::shared_ptr<HttpTransport> makeDownloadTransport(std::shared_ptr<HttpTransport>,
                                                         RetryPolicy,
                                                         std::shared_ptr<DownloadRequestGate>,
                                                         unsigned nRateLimitRetries,
                                                         std::chrono::seconds oRateLimitWait);
} // namespace Wenku8::Detail
