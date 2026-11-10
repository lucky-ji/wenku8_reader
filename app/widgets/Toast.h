// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Theme.h"
#include <QTimer>
namespace Wenku8::Reader
{
    class Toast final : public QWidget
    {
      public:
        explicit Toast(QWidget* pParent);
        void showText(const QString& strText, Theme eTheme);
      protected:
        bool eventFilter(QObject*, QEvent*) override;
        void paintEvent(QPaintEvent*) override;
      private:
        QString m_strText;
        Theme m_eTheme = Theme::Light;
        QTimer m_oTimer;
        void reposition();
    };
}
