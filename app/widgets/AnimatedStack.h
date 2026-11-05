// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QStackedWidget>
class QVariantAnimation;
namespace Wenku8::Reader
{
    class PageTransition;
    class AnimatedStack final : public QStackedWidget
    {
      public:
        using QStackedWidget::QStackedWidget;
        void setMotionEnabled(bool bEnabled);
        void setCurrentIndex(int nIndex);
        void returnTo(int nIndex);
        bool transitioning() const
        {
            return m_pOverlay != nullptr;
        }

      protected:
        void resizeEvent(QResizeEvent*) override;

      private:
        bool m_bMotion = false;
        PageTransition* m_pOverlay = nullptr;
        QVariantAnimation* m_pAnimation = nullptr;
        void changePage(int, bool);
        void finishTransition();
    };
} // namespace Wenku8::Reader
