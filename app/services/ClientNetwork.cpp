// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
namespace Wenku8::Reader
{
#ifdef WENKU8_ANDROID_ASSET_TLS
    std::shared_ptr<HttpTransport> makeAndroidAssetTransport();
    std::shared_ptr<HttpTransport> makeAndroidUpdateTransport();
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
    std::shared_ptr<HttpTransport> makeClientUpdateTransport()
    {
#ifdef WENKU8_ANDROID_ASSET_TLS
        return makeAndroidUpdateTransport();
#else
        return makeCurlTransport();
#endif
    }
} // namespace Wenku8::Reader
