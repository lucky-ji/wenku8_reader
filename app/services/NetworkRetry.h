// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8::Reader
{
    inline RetryOptions clientRetryOptions(bool bBrowser = false, bool bRateLimit = true)
    {
        RetryOptions oOptions;
        oOptions.m_nMaxAttempts = 3;
        oOptions.m_oDelay = std::chrono::milliseconds(400);
        oOptions.m_bExponentialBackoff = true;
        oOptions.m_oMaxDelay = std::chrono::seconds(15);
        oOptions.m_oAttemptTimeout = std::chrono::seconds(12);
        oOptions.m_bRetryBrowserNetworkErrors = bBrowser;
        oOptions.m_vHttpStatuses = {408, 425, 500, 502, 503, 504};
        if (bRateLimit)
            oOptions.m_vHttpStatuses.push_back(429);
        return oOptions;
    }
} // namespace Wenku8::Reader
