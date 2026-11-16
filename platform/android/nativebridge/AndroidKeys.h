// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QtGlobal>
#include <QMargins>
#include <functional>
namespace Wenku8::Reader
{
#ifdef Q_OS_ANDROID
    void setAndroidVolumePaging(bool bEnabled);
    void setAndroidReadingActive(bool bActive, bool bKeepAwake);
    // Physical pixels remaining inside the already fitted Android Activity content.
    QMargins getAndroidSafeInsets();
    // Set/clear on the Qt UI thread; notifications are queued to that same thread.
    void setAndroidSafeInsetsChangedHandler(std::function<void()> fnChanged);
#else
    inline void setAndroidVolumePaging(bool)
    {
    }
    inline void setAndroidReadingActive(bool, bool)
    {
    }
    inline QMargins getAndroidSafeInsets()
    {
        return {};
    }
    inline void setAndroidSafeInsetsChangedHandler(std::function<void()>)
    {
    }
#endif
} // namespace Wenku8::Reader
