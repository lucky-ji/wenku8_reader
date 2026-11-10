// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QWidget>
#include <QMap>
class QVBoxLayout;
class QLabel;
class QPushButton;
class QProgressBar;
namespace Wenku8::Reader
{
    class DownloadManager;
    class DownloadTaskPanel final : public QWidget
    {
      public:
        explicit DownloadTaskPanel(DownloadManager* pManager, QWidget* pParent = nullptr);
        void refresh();
      protected:
        void showEvent(QShowEvent*) override;
      private:
        struct Row
        {
            QWidget* m_pWidget = nullptr;
            QLabel *m_pTitle = nullptr, *m_pStatus = nullptr;
            QProgressBar* m_pProgress = nullptr;
            QPushButton *m_pPause = nullptr, *m_pRemove = nullptr;
        };
        DownloadManager* m_pManager;
        QVBoxLayout* m_pRows;
        QMap<QString, Row> m_vRows;
        QLabel* m_pEmpty;
        bool m_bRefreshPending = true;
    };
}
