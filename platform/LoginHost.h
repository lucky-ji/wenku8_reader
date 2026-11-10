// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#ifndef Q_MOC_RUN
#include "Wenku8Sdk.h"
#endif
#include "Theme.h"
#include <QObject>
#include <QUrl>
#include <memory>
class QWidget;
namespace Wenku8::Reader
{
    // UI-thread platform boundary. The transport is called on workers and owns
    // browser-session requests; credentials must never be exported into the SDK.
    class LoginHost : public QObject
    {
        Q_OBJECT
      public:
        using QObject::QObject;
        virtual bool visible() const = 0;
        virtual bool hasPage() const = 0;
        virtual bool persistent() const = 0;
        virtual void show(bool bReset) = 0;
        virtual void openLogin() = 0;
        virtual void accept() = 0;
        virtual void close() = 0;
        virtual void setHint(const QString&) = 0;
        virtual void setTheme(Theme) = 0;
        virtual std::string userAgent() const = 0;
        virtual std::shared_ptr<HttpTransport> transport() const = 0;
        // Independent request context, sharing browser-owned authentication only.
        virtual std::shared_ptr<HttpTransport> downloadTransport() const = 0;
      signals:
        void pageLoaded();
        void cancelled();
        void checkRequested();
        void unavailable();
    };
    std::unique_ptr<LoginHost> makeLoginHost(QWidget*, const QUrl&, const QString& strStorage);
} // namespace Wenku8::Reader
