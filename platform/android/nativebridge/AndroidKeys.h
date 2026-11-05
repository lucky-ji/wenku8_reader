// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QtGlobal>
namespace Wenku8::Reader
{
#ifdef Q_OS_ANDROID
    void setAndroidVolumePaging(bool bEnabled);
#else
    inline void setAndroidVolumePaging(bool)
    {
    }
#endif
} // namespace Wenku8::Reader
