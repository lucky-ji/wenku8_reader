// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Theme.h"
#include <QComboBox>
#include <QPointer>
namespace Wenku8::Reader
{
    class ThemePopup;
    class ThemeSelector final : public QComboBox
    {
      public:
        explicit ThemeSelector(QWidget* pParent = nullptr);
        ~ThemeSelector() override;
        void setTheme(Theme);
        void showPopup() override;
        void hidePopup() override;

      protected:
        void paintEvent(QPaintEvent*) override;

      private:
        Theme m_eTheme = Theme::Light;
        QPointer<ThemePopup> m_pPopup;
    };
} // namespace Wenku8::Reader
