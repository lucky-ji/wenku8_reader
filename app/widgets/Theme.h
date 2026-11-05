// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QColor>
#include <QWidget>
namespace Wenku8::Reader
{
    enum class Theme
    {
        Light,
        Dark,
        EyeCare
    };
    struct Colors
    {
        QColor m_oBackground, m_oSidebar, m_oPanel, m_oText, m_oMuted, m_oBorder, m_oHover,
            m_oAccent, m_oAccentText, m_oPaper, m_oSelection, m_oSelectionText, m_oDisabled,
            m_oSection;
    };
    Colors colors(Theme);
    QString scrollBarStyle(Theme);
    void applyTheme(QWidget*, Theme);
} // namespace Wenku8::Reader
