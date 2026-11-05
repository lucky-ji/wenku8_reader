// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8::Reader
{
    // Independent public image transport. Never shares browser session cookies.
    std::shared_ptr<HttpTransport> makeClientAssetTransport();
    RetryOptions clientAssetRetryOptions(bool bRateLimit = true);
    // One user operation may issue several HTTP requests. Share a single wall-clock
    // budget across them, instead of restarting the full timeout for every page.
    std::shared_ptr<HttpTransport> makeClientDeadlineTransport(std::shared_ptr<HttpTransport>,
                                                               std::chrono::milliseconds oBudget);
} // namespace Wenku8::Reader
