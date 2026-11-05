// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Theme.h"
#include <QCheckBox>
namespace Wenku8::Reader
{
    // A compact download setting with a full-row hit target. Native QCheckBox
    // keyboard, checked state, toggled signal and accessibility remain intact.
    class DownloadOption final : public QCheckBox
    {
      public:
        explicit DownloadOption(QWidget* pParent = nullptr);
        void setTouchUi(bool);
        void setTheme(Theme);
        QSize sizeHint() const override;
        QSize minimumSizeHint() const override;

      protected:
        void paintEvent(QPaintEvent*) override;
        bool hitButton(const QPoint&) const override;
        void enterEvent(QEvent*) override;
        void leaveEvent(QEvent*) override;
        void changeEvent(QEvent*) override;

      private:
        bool m_bTouchUi = false;
        Theme m_eTheme = Theme::Light;
        QFont optionFont() const;
    };
} // namespace Wenku8::Reader
