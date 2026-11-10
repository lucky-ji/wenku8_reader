// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QStackedWidget>
#include <QPointer>
class QVariantAnimation;
namespace Wenku8::Reader
{
    class AnimatedStack final : public QStackedWidget
    {
      public:
        using QStackedWidget::QStackedWidget;
        void setMotionEnabled(bool bEnabled);
        void setCurrentIndex(int nIndex);
        void returnTo(int nIndex);
        bool transitioning() const
        {
            return m_pAnimation != nullptr;
        }

      protected:
        bool event(QEvent*) override;
        void resizeEvent(QResizeEvent*) override;

      private:
        bool m_bMotion = false, m_bHooksConnected = false;
        QPointer<QWidget> m_pAnimatedPage;
        QPoint m_oRestPosition;
        QVariantAnimation* m_pAnimation = nullptr;
        quint64 m_nTransitionGeneration = 0;
        void changePage(int, bool);
        void finishTransition();
        void connectTransitionHooks();
    };
} // namespace Wenku8::Reader
