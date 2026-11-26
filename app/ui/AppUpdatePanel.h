// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QUrl>
#include <QWidget>
class QLabel;
class QProgressBar;
class QPushButton;
namespace Wenku8::Reader
{
    class AppUpdateCheck;

    // An explicit, non-blocking check in the account page. It never downloads
    // packages or changes the active reading/business page.
    class AppUpdatePanel final : public QWidget
    {
      public:
        explicit AppUpdatePanel(QWidget* pParent = nullptr);

      private:
        AppUpdateCheck* m_pCheck = nullptr;
        QLabel* m_pStatus = nullptr;
        QProgressBar* m_pProgress = nullptr;
        QPushButton* m_pCheckButton = nullptr;
        QPushButton* m_pReleaseButton = nullptr;
        QUrl m_oReleaseUrl;
        bool m_bChecking = false;
        void startCheck();
        void openRelease();
    };
} // namespace Wenku8::Reader
