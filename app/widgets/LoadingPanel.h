// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Theme.h"
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
class QLabel;
class QPushButton;
namespace Wenku8::Reader
{
    // Sibling overlay for the application shell. The owner should disable the shell
    // while this is visible; the cancel action deliberately remains outside it.
    class LoadingPanel final : public QWidget
    {
      public:
        explicit LoadingPanel(QWidget* pParent = nullptr);
        void setTheme(Theme);
        void setMessage(const QString&);
        void setProgress(qint64 nCurrent, qint64 nTotal);
        void setCancelable(bool);
        void setCancelling(bool);
        std::function<void()> m_fnOnCancel;

      protected:
        void paintEvent(QPaintEvent*) override;
        void resizeEvent(QResizeEvent*) override;
        void showEvent(QShowEvent*) override;
        void hideEvent(QHideEvent*) override;
        void keyPressEvent(QKeyEvent*) override;
        void wheelEvent(QWheelEvent*) override;
        void mousePressEvent(QMouseEvent*) override;
        bool event(QEvent*) override;

      private:
        Theme m_eTheme = Theme::Light;
        QTimer m_oAnimation;
        QElapsedTimer m_oElapsed;
        QLabel *m_pTitle, *m_pMessage, *m_pProgressText;
        QPushButton* m_pCancel;
        QRect m_oCard, m_oRing, m_oStripe;
        qint64 m_nCurrent = 0, m_nTotal = 0;
        bool m_bCancelable = false, m_bCancelling = false;
        void arrange();
        bool startup() const;
    };
} // namespace Wenku8::Reader
