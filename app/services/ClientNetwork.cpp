// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
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
    RetryProfile assetRetryProfile()
    {
#ifdef WENKU8_ANDROID_ASSET_TLS
        return RetryProfile::Browser;
#else
        return RetryProfile::Standard;
#endif
    }
} // namespace Wenku8::Reader
