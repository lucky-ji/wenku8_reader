// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8::Reader
{
    // Independent public image transport. Never shares browser session cookies.
    std::shared_ptr<HttpTransport> makeClientAssetTransport();
    RetryProfile assetRetryProfile();
    std::shared_ptr<HttpTransport> makeClientUpdateTransport();
} // namespace Wenku8::Reader
