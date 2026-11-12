// SPDX-License-Identifier: GPL-2.0-only
#include "Theme.h"
#include "ThemeSelector.h"
#include "Icons.h"
#include "DownloadOption.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPalette>
#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QLabel>
namespace Wenku8::Reader
{
    namespace
    {
        void setWidgetStyle(QWidget* pWidget, const QString& strStyle)
        {
            if (pWidget->styleSheet() != strStyle)
            {
                pWidget->setStyleSheet(strStyle);
            }
        }
    }

    Colors colors(Theme eTheme)
    {
        if (eTheme == Theme::Dark)
            return {"#202630", "#171c24", "#29313d", "#eef2f8", "#b1bccd", "#465266", "#34475f",
                    "#94c7ae", "#12281e", "#232a32", "#2a4d70", "#e0efff", "#778296", "#252c36"};
        if (eTheme == Theme::EyeCare)
            return {"#f3eddd", "#e7dfcb", "#fbf6e8", "#344035", "#68735e", "#cec4a8", "#e5ebd7",
                    "#496744", "#ffffff", "#f6efda", "#d9e4bd", "#30472b", "#969780", "#ece4d1"};
        return {"#f8fafc", "#edf1f6", "#ffffff", "#233044", "#626d7c", "#ced7e3", "#e7eff9",
                "#315b46", "#ffffff", "#ffffff", "#dceaff", "#174d90", "#929dad", "#edf0f5"};
    }
    QString scrollBarStyle(Theme eTheme)
    {
        const auto oC = colors(eTheme);
        const QString strThumb = eTheme == Theme::Dark      ? "#63748c"
                                 : eTheme == Theme::EyeCare ? "#a19b7d"
                                                            : "#9aa8bb";
        const QString strHover = eTheme == Theme::Dark      ? "#91aecf"
                                 : eTheme == Theme::EyeCare ? "#7d8d62"
                                                            : "#6f85a4";
        const QString strPressed = eTheme == Theme::Dark      ? "#b9d7f3"
                                   : eTheme == Theme::EyeCare ? "#496744"
                                                              : "#436c9d";
        // Keep the hit area stable; only the painted thumb expands on hover.
        return QString(R"(
        QScrollBar:vertical { background:transparent; width:14px; margin:6px 1px; border:0; border-radius:6px; }
        QScrollBar:horizontal { background:transparent; height:14px; margin:1px 6px; border:0; border-radius:6px; }
        QScrollBar:vertical:hover, QScrollBar:horizontal:hover { background:%1; }
        QScrollBar::handle:vertical { background:%2; margin:0px 3px; border:0; min-height:48px; border-radius:3px; }
        QScrollBar::handle:horizontal { background:%2; margin:3px 0px; border:0; min-width:48px; border-radius:3px; }
        QScrollBar::handle:vertical:hover { background:%3; margin:0px 2px; border-radius:4px; }
        QScrollBar::handle:horizontal:hover { background:%3; margin:2px 0px; border-radius:4px; }
        QScrollBar::handle:vertical:pressed, QScrollBar::handle:horizontal:pressed { background:%4; }
        QScrollBar::handle:vertical:disabled, QScrollBar::handle:horizontal:disabled { background:%5; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; border:0; background:transparent; subcontrol-origin:margin; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; border:0; background:transparent; subcontrol-origin:margin; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; border:0; }
        QScrollBar::up-arrow, QScrollBar::down-arrow, QScrollBar::left-arrow, QScrollBar::right-arrow { width:0; height:0; image:none; }
        QAbstractScrollArea::corner { background:transparent; border:0; }
    )")
            .arg(oC.m_oSection.name(), strThumb, strHover, strPressed, oC.m_oBorder.name());
    }
    void applyTheme(QWidget* pWidget, Theme eTheme)
    {
        const auto oC = colors(eTheme);
        QPalette oP;
        oP.setColor(QPalette::Window, oC.m_oBackground);
        oP.setColor(QPalette::WindowText, oC.m_oText);
        oP.setColor(QPalette::Base, oC.m_oPanel);
        oP.setColor(QPalette::AlternateBase, oC.m_oHover);
        oP.setColor(QPalette::Text, oC.m_oText);
        oP.setColor(QPalette::Button, oC.m_oPanel);
        oP.setColor(QPalette::ButtonText, oC.m_oText);
        oP.setColor(QPalette::Highlight, oC.m_oSelection);
        oP.setColor(QPalette::HighlightedText, oC.m_oSelectionText);
        oP.setColor(QPalette::PlaceholderText, oC.m_oMuted);
        oP.setColor(QPalette::Disabled, QPalette::Text, oC.m_oDisabled);
        oP.setColor(QPalette::Disabled, QPalette::ButtonText, oC.m_oDisabled);
        // Creating a secondary page must not repolish every window and the reader.
        if (qApp->palette() != oP)
        {
            qApp->setPalette(oP);
        }
        if (pWidget->palette() != oP)
        {
            pWidget->setPalette(oP);
        }
        QString strSheet =
            QString(R"(
        QWidget { color:%1; font-size:13px; }
        QMainWindow, QDialog, QWidget#shell { background:%2; }
        QFrame#sidebar { background:%3; border-right:1px solid %4; }
        QFrame#surface { background:%5; border:1px solid %4; border-radius:16px; }
        QLabel { background:transparent; }
        QLabel#brand { font-size:22px; font-weight:700; letter-spacing:1px; }
        QLabel#pageTitle { font-size:28px; font-weight:600; }
        QLabel#sectionTitle { color:%6; font-size:12px; font-weight:600; padding:6px 2px; }
        QLabel#readerProgress { color:%6; font-size:11px; padding:2px 8px; }
        QWidget#downloadTaskRow { background:%5; border:1px solid %4; border-radius:12px; }
        QLabel#taskTitle { font-weight:600; }
        QLabel#taskMeta { color:%6; font-size:12px; }
        QMenuBar { background:%2; color:%1; padding:3px 8px; border-bottom:1px solid %4; }
        QMenuBar::item { background:transparent; padding:5px 12px; border-radius:6px; }
        QMenuBar::item:selected { background:%7; color:%1; }
        QMenu { background:%5; color:%1; border:1px solid %4; border-radius:10px; padding:6px; }
        QMenu::item { padding:9px 24px 9px 14px; border-radius:6px; }
        QMenu::item:selected { background:%10; color:%11; }
        QMenu::item:disabled { color:%12; }
        QMenu::separator { height:1px; background:%4; margin:5px 8px; }
        QLabel#bookTitle { font-size:25px; font-weight:600; }
        QLabel#eyebrow { color:%6; font-size:11px; letter-spacing:2px; }
        QLabel#subtitle, QLabel#muted, QLabel#status, QLabel#listCount { color:%6; }
        QLabel#intro { font-size:14px; }
        QLabel#coverImage { background:%13; border:1px solid %4; border-radius:10px; color:%6; }
        QPushButton { background:%5; border:1px solid %4; border-radius:9px; padding:9px 14px; }
        QPushButton:hover { background:%7; }
        QPushButton:pressed { border-color:%8; }
        QPushButton:disabled { color:%6; }
        QPushButton#primary { background:%8; color:%9; border:0; font-weight:600; }
        QPushButton#nav { text-align:left; background:transparent; border:0; padding:12px 14px; border-radius:9px; }
        QPushButton#nav:hover, QPushButton#nav:checked { background:%7; }
        QPushButton#nav:checked { font-weight:600; }
        QPushButton#quiet { background:transparent; border:0; color:%6; }
        QLineEdit, QComboBox { color:%1; background:%5; border:1px solid %4; border-radius:9px; padding:10px 12px; selection-background-color:%8; }
        QLineEdit:focus, QComboBox:focus { border-color:%8; }
        QComboBox:hover { border-color:%11; background:%7; }
        QComboBox::drop-down { border:0; width:24px; }
        QComboBox QAbstractItemView { background:%5; color:%1; selection-background-color:%10; selection-color:%11; border:1px solid %4; padding:6px; }
        QListWidget#cards { background:transparent; border:0; outline:0; }
        QTreeWidget { background:transparent; border:0; outline:0; }
        QTreeWidget::item { padding:10px 6px; border-radius:6px; }
        QTreeWidget::item:selected { background:%10; color:%11; }
        QListWidget:not(#cards) { background:%5; border:1px solid %4; border-radius:10px; padding:6px; }
        QListWidget:not(#cards)::item { padding:10px; border-bottom:1px solid %4; border-radius:6px; }
        QListWidget:not(#cards)::item:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %7,stop:1 %5); }
        QListWidget:not(#cards)::item:selected { background:%10; color:%11; }
        QCheckBox { padding:7px; spacing:10px; }
        QCheckBox::indicator { width:18px; height:18px; }
        QCheckBox:hover { color:%11; background:%7; border-radius:6px; }
        QCheckBox:disabled { color:%12; background:transparent; }
        QTextBrowser { border:0; }
        QSplitter::handle { background:%4; width:1px; }
        QProgressBar { background:%7; border:0; height:4px; border-radius:2px; color:transparent; }
        QProgressBar::chunk { background:%8; border-radius:2px; }
        QToolTip { background:%5; color:%1; border:1px solid %4; padding:6px; }
    )")
                .arg(oC.m_oText.name(), oC.m_oBackground.name(), oC.m_oSidebar.name(),
                     oC.m_oBorder.name(), oC.m_oPanel.name(), oC.m_oMuted.name(),
                     oC.m_oHover.name(), oC.m_oAccent.name(), oC.m_oAccentText.name())
                .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name(), oC.m_oDisabled.name(),
                     oC.m_oSection.name());
        const auto oCheckFill = eTheme == Theme::Dark ? oC.m_oAccent.darker(230) : oC.m_oAccent;
        const QString strCheckStyle = QString(
                        "QCheckBox::indicator { background:%1; border:1px solid %2; "
                        "border-radius:5px; image:none; } QCheckBox::indicator:hover { "
                        "border-color:%3; } QCheckBox::indicator:checked { background:%3; "
                        "border-color:%3; image:url(:/wenku8/icons/check.svg); } "
                        "QCheckBox::indicator:disabled { background:%4; border-color:%2; } "
                        "QCheckBox::indicator:checked:disabled { background:%5; border-color:%5; }")
                        .arg(oC.m_oPanel.name(), oC.m_oBorder.name(), oCheckFill.name(),
                             oC.m_oSection.name(), oC.m_oDisabled.darker(120).name());
        strSheet += strCheckStyle;
        if (pWidget->property("touchUi").toBool())
        {
            const auto oBar = eTheme == Theme::Dark      ? QColor("#131c28")
                              : eTheme == Theme::EyeCare ? QColor("#e3d9bf")
                                                         : QColor("#e7eef8");
            strSheet +=
                QString(
                    "QWidget#mobileNavigation { background:%1; border:1px solid %2; border-top:1px "
                    "solid %2; border-radius:16px; } QWidget#readerFooter { background:%5; "
                    "border-top:1px solid %2; border-radius:0; } QWidget#readerTop { background:%5; "
                    "border-bottom:1px solid %2; } QLabel#status { background:%3; "
                    "color:%4; border-radius:8px; padding:6px 8px; font-size:11px; }")
                    .arg(oBar.name(), oC.m_oBorder.name(), oC.m_oSection.name(),
                         oC.m_oMuted.name(), oC.m_oPanel.name());
            strSheet += "QCheckBox::indicator { width:20px; height:20px; }";
            // Dynamic task actions inherit this role before the next theme application.
            strSheet += QString(
                            "QPushButton[textAction=\"true\"] { background:transparent; "
                            "color:%1; border:0; border-radius:10px; padding:12px 14px; "
                            "font-size:14px; } QPushButton[textAction=\"true\"]:hover, "
                            "QPushButton[textAction=\"true\"]:focus { background:transparent; "
                            "border:0; } QPushButton[textAction=\"true\"]:pressed { "
                            "background:%2; } QPushButton[textAction=\"true\"]:disabled { "
                            "background:transparent; color:%3; }")
                            .arg(oC.m_oText.name(), oC.m_oSelection.name(), oC.m_oDisabled.name());
        }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        strSheet += "QPushButton, QToolButton { outline:0; }";
#endif
        setWidgetStyle(pWidget, strSheet + scrollBarStyle(eTheme));
        for (auto pLabel : pWidget->findChildren<QLabel*>())
            if (pLabel->objectName() == "muted" || pLabel->objectName() == "subtitle" ||
                pLabel->objectName() == "eyebrow")
                setWidgetStyle(pLabel, "QLabel { color:" + oC.m_oMuted.name() + "; }");
        // Qt 5 caches inherited palettes on reparented/polished mobile controls.
        // Give the fixed chrome an explicit surface and text color in each theme.
        if (pWidget->property("touchUi").toBool())
        {
            const auto oBar = eTheme == Theme::Dark      ? QColor("#131c28")
                              : eTheme == Theme::EyeCare ? QColor("#e3d9bf")
                                                         : QColor("#e7eef8");
            if (auto pNavigation = pWidget->findChild<QWidget*>("mobileNavigation"))
                setWidgetStyle(pNavigation,
                    QString("QWidget#mobileNavigation { background:%1; border:1px solid %2; "
                            "border-top:1px solid %2; border-radius:16px; }")
                        .arg(oBar.name(), oC.m_oBorder.name()));
            if (auto pFooter = pWidget->findChild<QWidget*>("readerFooter"))
                setWidgetStyle(pFooter, QString("QWidget#readerFooter { background:%1; "
                                               "border-top:1px solid %2; border-radius:0; }")
                                           .arg(oC.m_oPanel.name(), oC.m_oBorder.name()));
            if (auto pReaderTop = pWidget->findChild<QWidget*>("readerTop"))
                setWidgetStyle(pReaderTop, QString("QWidget#readerTop { background:%1; "
                                                  "border-bottom:1px solid %2; }")
                                              .arg(oC.m_oPanel.name(), oC.m_oBorder.name()));
            if (auto pStatus = pWidget->findChild<QLabel*>("status"))
                setWidgetStyle(pStatus,
                    QString("QLabel { color:%1; background:%2; border-radius:8px; padding:6px 8px; "
                            "font-size:11px; }")
                        .arg(oC.m_oMuted.name(), oC.m_oSection.name()));
            if (auto pHeading = pWidget->findChild<QWidget*>("mobileHeader"))
                for (auto pLabel : pHeading->findChildren<QLabel*>())
                    setWidgetStyle(pLabel, "QLabel { color:" + oC.m_oText.name() + "; }");
        }
        else
        {
            if (auto pStatus = pWidget->findChild<QLabel*>("status"))
            {
                setWidgetStyle(pStatus, QString());
            }
            if (auto pHeading = pWidget->findChild<QWidget*>("mobileHeader"))
            {
                for (auto pLabel : pHeading->findChildren<QLabel*>())
                {
                    setWidgetStyle(pLabel, QString());
                }
            }
            for (const auto& strName : {QStringLiteral("readerTop"), QStringLiteral("readerFooter")})
            {
                if (auto pChrome = pWidget->findChild<QWidget*>(strName))
                {
                    setWidgetStyle(pChrome, QString());
                }
            }
        }
        for (auto* pCheck : pWidget->findChildren<QCheckBox*>())
        {
            if (dynamic_cast<DownloadOption*>(pCheck))
            {
                continue;
            }
            // Reparented secondary pages can retain Qt 5's native indicator cache.
            setWidgetStyle(pCheck, strCheckStyle +
                QString("QCheckBox { padding:7px; spacing:10px; color:%1; } "
                        "QCheckBox::indicator { width:%2px; height:%2px; } "
                        "QCheckBox:disabled { color:%3; }")
                    .arg(oC.m_oText.name())
                    .arg(pWidget->property("touchUi").toBool() ? 20 : 18)
                    .arg(oC.m_oDisabled.name()));
        }
        for (auto pBar : pWidget->findChildren<QScrollBar*>())
            setWidgetStyle(pBar, scrollBarStyle(eTheme));
        // Local control styles avoid Qt 5 palette caching when switching an already polished
        // window.
        for (auto pInput : pWidget->findChildren<QLineEdit*>())
        {
            pInput->setPalette(oP);
            pInput->setProperty("hintColor", oC.m_oMuted);
            pInput->update();
            setWidgetStyle(pInput,
                QString(
                    "QLineEdit { color:%1; background:%2; border:1px solid %3; border-radius:9px; "
                    "padding:10px 12px; selection-background-color:%4; } QLineEdit:hover { "
                    "border-color:%4; } QLineEdit:focus { border:2px solid %4; padding:9px 11px; }")
                    .arg(oC.m_oText.name(), oC.m_oPanel.name(), oC.m_oBorder.name(),
                         oC.m_oAccent.name()));
        }
        for (auto pButton : pWidget->findChildren<QPushButton*>())
        {
            const auto strRole = pButton->objectName();
            if (strRole == "loadingCancel")
                continue;
            QString strBackground = oC.m_oPanel.name(), strForeground = oC.m_oText.name(),
                    strBorder = "1px solid " + oC.m_oBorder.name();
            if (strRole == "nav" || strRole == "quiet")
            {
                strBackground = "transparent";
                strBorder = "0";
                if (strRole == "quiet")
                    strForeground = oC.m_oMuted.name();
            }
            if (strRole == "primary" || pButton->property("primary").toBool())
            {
                strBackground = oC.m_oAccent.name();
                strForeground = oC.m_oAccentText.name();
                strBorder = "0";
            }
            const bool bPrimary = strRole == "primary" || pButton->property("primary").toBool();
            const auto oHover = bPrimary ? oC.m_oAccent.darker(112) : oC.m_oHover;
            const auto oPressed = bPrimary ? oC.m_oAccent.darker(125) : oC.m_oSelection;
            QString strStyle =
                QString(
                    "QPushButton { background:%1; color:%2; border:%3; border-radius:9px; "
                    "padding:9px 14px; } QPushButton:hover { background:%4; border:1px solid "
                    "%5; } QPushButton:pressed { background:%6; } QPushButton:disabled { color:%7; "
                    "background:%8; border:1px solid %9; }")
                    .arg(strBackground, strForeground, strBorder, oHover.name(),
                         oC.m_oSelectionText.name(), oPressed.name(), oC.m_oDisabled.name(),
                         oC.m_oSection.name(), oC.m_oBorder.name());
            if (strRole == "nav")
                strStyle +=
                    QString("QPushButton { text-align:left; padding:12px 14px; } QPushButton:hover "
                            "{ border:1px solid %1; padding:11px 13px; } QPushButton:checked { "
                            "background:%2; color:%3; border-left:3px solid %3; font-weight:600; "
                            "padding-left:11px; } QPushButton:checked:hover { background:%4; }")
                        .arg(oC.m_oBorder.name(), oC.m_oSelection.name(),
                             oC.m_oSelectionText.name(), oC.m_oSelection.lighter(106).name());
            if (strRole == "mobileNav")
                strStyle +=
                    QString("QPushButton { padding:6px 4px; font-size:12px; } QPushButton:checked "
                            "{ background:%1; color:%2; border:1px solid %2; font-weight:600; } "
                            "QPushButton:focus { padding:5px 3px; }")
                        .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name());
            if (pWidget->property("touchUi").toBool())
                strStyle +=
                    QString("QPushButton { padding:12px 14px; } QPushButton:hover { background:%1; "
                            "border:%2; } QPushButton:pressed { background:%3; } QPushButton:focus "
                            "{ border:1px solid %4; padding:12px 14px; }")
                        .arg(strBackground, strBorder, oPressed.name(), oC.m_oBorder.name());
            if (strRole == "themeChoice")
                strStyle +=
                    QString("QPushButton { text-align:left; padding:16px; font-size:15px; } "
                            "QPushButton:checked, QPushButton:checked:hover { background:%1; "
                            "color:%2; border:2px solid %2; } QPushButton:focus { padding:16px; }")
                        .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name());
            if (strRole == "mobileNav")
                strStyle += QString("QPushButton { padding:6px 4px; } QPushButton:checked, "
                                    "QPushButton:checked:hover { background:%1; color:%2; "
                                    "border:1px solid %2; }")
                                .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name());
            if (pButton->property("mobileRow").toBool())
                strStyle += "QPushButton { text-align:left; padding:14px 16px; font-size:14px; } "
                            "QPushButton:focus { padding:14px 16px; }";
            if (pWidget->property("touchUi").toBool())
            {
                const bool bRow = pButton->property("mobileRow").toBool(),
                           bThemeChoice = strRole == "themeChoice",
                           bTextAction = pButton->property("textAction").toBool() && !bPrimary;
                const auto oFill = bPrimary ? oC.m_oAccent : oC.m_oSection;
                const auto oInk = bPrimary ? oC.m_oAccentText : oC.m_oText;
                const QString strFill = bTextAction ? QStringLiteral("transparent") : oFill.name();
                // Touch hover often persists after release. Keep only a momentary pressed
                // surface, and never change border widths or padding when focus moves.
                strStyle =
                    QString("QPushButton { background:%1; color:%2; border:0; border-radius:10px; "
                            "padding:12px 14px; font-size:14px; } QPushButton:hover { "
                            "background:%1; } QPushButton:pressed { background:%3; } "
                            "QPushButton:disabled { background:%4; color:%5; } "
                            "QPushButton:focus { border:0; padding:12px 14px; }")
                        .arg(strFill, oInk.name(),
                             (bPrimary ? oC.m_oAccent.darker(112) : oC.m_oSelection).name(),
                             bTextAction ? QStringLiteral("transparent") : oC.m_oSection.name(),
                             oC.m_oDisabled.name());
                if (bRow)
                    strStyle += "QPushButton { text-align:left; padding:14px 16px; } "
                                "QPushButton:focus { padding:14px 16px; }";
                if (bThemeChoice)
                    strStyle +=
                        QString(
                            "QPushButton { text-align:left; padding:14px; border:2px solid "
                            "transparent; } QPushButton:checked, QPushButton:checked:hover { "
                            "background:%1; color:%2; border:2px solid %2; } "
                            "QPushButton:focus { padding:14px; border:2px solid transparent; } "
                            "QPushButton:checked:focus { border:2px solid %2; }")
                            .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name());
                pButton->setIconSize(QSize(22, 22));
            }
            if (strRole == "mobileNav")
            {
                pButton->setProperty("navMuted", oC.m_oMuted);
                pButton->setProperty("navActive", oC.m_oSelectionText);
                pButton->setProperty("navSelection", oC.m_oSelection);
                pButton->setProperty("navPressed", oC.m_oHover);
                pButton->setProperty("navDisabled", oC.m_oDisabled);
                strStyle = "QPushButton, QPushButton:hover, QPushButton:checked, "
                           "QPushButton:pressed { background:transparent; border:0; padding:0; }";
            }
            if (strRole == "readingModeChoice")
                strStyle += QString("QPushButton:checked, QPushButton:checked:hover { "
                                    "background:%1; color:%2; border:1px solid %2; }")
                                .arg(oC.m_oSelection.name(), oC.m_oSelectionText.name());
            setWidgetStyle(pButton, strStyle);
            if (pWidget->property("touchUi").toBool() &&
                pButton->property("textAction").toBool() && !bPrimary)
            {
                // Keep the icon identity for desktop resizing; mobile chrome uses text only.
                if (!pButton->icon().isNull())
                {
                    pButton->setIcon(QIcon());
                }
            }
            else if (pButton->property("wenkuIcon").isValid())
            {
                const QColor oInk(strForeground);
                pButton->setIcon(makeIcon(Icon(pButton->property("wenkuIcon").toInt()), oInk,
                                          oC.m_oDisabled, bPrimary ? oInk : oC.m_oSelectionText));
            }
        }
        if (auto pThemes = pWidget->findChild<QComboBox*>("themeSelector"))
        {
            for (int nI = 0; nI < 3; ++nI)
                pThemes->setItemIcon(nI, makeIcon(nI == 0   ? Icon::Sun
                                                  : nI == 1 ? Icon::Moon
                                                            : Icon::Leaf,
                                                  oC.m_oText, oC.m_oMuted, oC.m_oAccent));
            if (auto pSelector = dynamic_cast<ThemeSelector*>(pThemes))
                pSelector->setTheme(eTheme);
        }
        pWidget->setWindowIcon(makeIcon(Icon::Book, oC.m_oAccent, oC.m_oMuted, oC.m_oAccent));
    }
} // namespace Wenku8::Reader
