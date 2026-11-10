// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
#include <QUrl>
class QWidget;
class QWebEngineView;
namespace Wenku8::QtWebView
{
    class BrowserBridge;
    // Construct/use/destroy on the QApplication thread. The transport is used on workers.
    // By default a dedicated off-the-record profile is shared by the view and fetch transport.
    class BrowserSession
    {
      public:
        explicit BrowserSession(const QUrl& oOrigin, QWidget* pViewParent = nullptr,
                                const QString& strPersistentDirectory = {});
        ~BrowserSession();
        BrowserSession(const BrowserSession&) = delete;
        BrowserSession& operator=(const BrowserSession&) = delete;
        QWebEngineView* view() const;
        std::string userAgent() const;
        void openLogin();
        bool persistent() const;
        void setPageStyle(const QString& strCss);
        void reset(); // Discards this app's profile, never another browser's cookies.
        void close(); // Cancels pending work; call before shutting down QApplication.
        std::shared_ptr<HttpTransport> transport() const;
        // Independent hidden document, sharing this session's in-memory profile.
        // Remains usable after reset; closed permanently with the session.
        std::shared_ptr<HttpTransport> downloadTransport() const;

      private:
        std::shared_ptr<BrowserBridge> m_pBridge;
    };
} // namespace Wenku8::QtWebView
