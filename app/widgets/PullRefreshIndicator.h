// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Theme.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantAnimation>
namespace Wenku8::Reader
{
    // A passive viewport overlay. The owner positions it near the upper centre;
    // neither its geometry nor its animation changes the scroll area's layout.
    class PullRefreshIndicator final : public QWidget
    {
      public:
        explicit PullRefreshIndicator(QWidget* pParent = nullptr);
        void setTheme(Theme);
        void setProgress(qreal fProgress);
        void setRefreshing(bool);
        QSize sizeHint() const override
        {
            return {220, 40};
        }
        QSize minimumSizeHint() const override
        {
            return {150, 40};
        }

      protected:
        void paintEvent(QPaintEvent*) override;
        void showEvent(QShowEvent*) override;
        void hideEvent(QHideEvent*) override;

      private:
        Theme m_eTheme = Theme::Light;
        qreal m_fProgress = 0, m_fOpacity = 1;
        bool m_bRefreshing = false;
        QElapsedTimer m_oClock;
        QTimer m_oSpin;
        QVariantAnimation m_oDismissal;
        void synchronize();
        void dismiss();
    };
} // namespace Wenku8::Reader
