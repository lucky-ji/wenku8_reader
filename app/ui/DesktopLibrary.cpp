// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#include "Icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>

namespace Wenku8::Reader
{
    void ClientWindow::buildDesktopLibrary(QHBoxLayout* pBar)
    {
        m_bDesktopShelfList = m_oSettings.value("desktopLibraryList", false).toBool();
        m_pDesktopClearSearch = new QPushButton("退出搜索");
        m_pDesktopClearSearch->setObjectName("desktopClearSearch");
        m_pDesktopClearSearch->setProperty("textAction", true);
        pBar->addWidget(m_pDesktopClearSearch);
        m_pDesktopClearSearch->hide();
        connect(m_pDesktopClearSearch, &QPushButton::clicked, this, [this]
        {
            navigate(m_nSection);
            if (desktopLibraryActive())
            {
                m_pSearch->setFocus(Qt::OtherFocusReason);
            }
        });

        m_pDesktopShelfView = new QWidget;
        m_pDesktopShelfView->setObjectName("desktopLibraryView");
        m_pDesktopShelfView->setAttribute(Qt::WA_StyledBackground);
        auto* pLayout = new QHBoxLayout(m_pDesktopShelfView);
        pLayout->setContentsMargins(3, 3, 3, 3);
        pLayout->setSpacing(2);
        auto* pGroup = new QButtonGroup(m_pDesktopShelfView);
        m_pDesktopGrid = new QPushButton;
        m_pDesktopGrid->setObjectName("desktopLibraryGrid");
        m_pDesktopList = new QPushButton;
        m_pDesktopList->setObjectName("desktopLibraryList");
        for (auto* pChoice : {m_pDesktopGrid, m_pDesktopList})
        {
            const bool bList = pChoice == m_pDesktopList;
            pChoice->setAccessibleName(bList ? "紧凑列表" : "封面网格");
            pChoice->setToolTip(pChoice->accessibleName());
            pChoice->setCheckable(true);
            pChoice->setChecked(bList == m_bDesktopShelfList);
            pChoice->setFixedSize(38, 30);
            pChoice->setCursor(Qt::PointingHandCursor);
            setButtonIcon(pChoice, bList ? Icon::List : Icon::Library);
            pGroup->addButton(pChoice);
            pLayout->addWidget(pChoice);
            connect(pChoice, &QPushButton::clicked, this, [this, bList]
            {
                if (m_bDesktopShelfList != bList)
                {
                    m_bDesktopShelfList = bList;
                    m_oSettings.setValue("desktopLibraryList", bList);
                    updateDesktopLibraryLayout();
                }
            });
        }
        pBar->addWidget(m_pDesktopShelfView);
        m_pDesktopShelfView->hide();

        auto* pFocusSearch = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
        connect(pFocusSearch, &QShortcut::activated, this, [this]
        {
            if (desktopLibraryActive())
            {
                m_pSearch->setFocus(Qt::ShortcutFocusReason);
                m_pSearch->selectAll();
            }
        });
        m_vDesktopLibraryShortcuts.append(pFocusSearch);
        for (int nSection = 0; nSection < m_vNav.size(); ++nSection)
        {
            auto* pShortcut = new QShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + nSection)), this);
            connect(pShortcut, &QShortcut::activated, this, [this, nSection]
            {
                if (desktopLibraryActive())
                {
                    navigate(nSection);
                }
            });
            m_vDesktopLibraryShortcuts.append(pShortcut);
        }
        for (auto* pShortcut : m_vDesktopLibraryShortcuts)
        {
            pShortcut->setEnabled(false);
            pShortcut->setAutoRepeat(false);
        }
    }

    bool ClientWindow::desktopLibraryActive() const
    {
        return !mobileMode() && m_bUnlocked && !m_bLoadingBusy && !m_bReaderFullscreen &&
            m_pPages->currentIndex() == 1 && m_pStartupPages->currentIndex() == 1;
    }

    bool ClientWindow::handleDesktopLibraryKey(QObject* pWatched, QKeyEvent* pEvent)
    {
        if ((pWatched != m_pSearch && pWatched != m_pCards) || !desktopLibraryActive() ||
            (pEvent->modifiers() != Qt::NoModifier && pEvent->modifiers() != Qt::KeypadModifier))
        {
            return false;
        }
        if (pWatched == m_pSearch && pEvent->key() == Qt::Key_Down && m_pCards->count() > 0)
        {
            if (!m_pCards->currentItem())
            {
                m_pCards->setCurrentRow(0);
            }
            m_pCards->setFocus(Qt::ShortcutFocusReason);
            return true;
        }
        if (pWatched == m_pCards &&
            (pEvent->key() == Qt::Key_Return || pEvent->key() == Qt::Key_Enter))
        {
            if (auto* pItem = m_pCards->currentItem(); pItem && !pEvent->isAutoRepeat())
            {
                openBook(pItem->data(Qt::UserRole).toInt(),
                         m_nSection == 3 && m_strActiveSearch.isEmpty());
            }
            return true;
        }
        return false;
    }

    void ClientWindow::applyDesktopLibraryStyle()
    {
        const auto oColors = colors(m_eTheme);
        if (!mobileMode())
        {
            m_pSearch->setStyleSheet(QString("QLineEdit#novelSearch {selection-color:%1}")
                                        .arg(oColors.m_oAccentText.name()));
        }
        m_pDesktopShelfView->setStyleSheet(
            QString("QWidget#desktopLibraryView {background:%1; border:1px solid %2; "
                    "border-radius:10px}").arg(oColors.m_oSection.name(), oColors.m_oBorder.name()));
        const QString strButtonStyle = QString(
            "QPushButton {background:transparent; border:0; border-radius:7px; padding:0} "
            "QPushButton:hover:enabled {background:%1} "
            "QPushButton:checked {background:%2} "
            "QPushButton:pressed:enabled {background:%2}")
            .arg(oColors.m_oHover.name(), oColors.m_oSelection.name());
        m_pDesktopGrid->setStyleSheet(strButtonStyle);
        m_pDesktopList->setStyleSheet(strButtonStyle);
        const QString strNavStyle = QString(
            "QPushButton#nav:hover:enabled {background:%1} "
            "QPushButton#nav:checked {background:%2; color:%3}")
            .arg(oColors.m_oHover.name(), oColors.m_oSelection.name(),
                 oColors.m_oSelectionText.name());
        for (auto* pNav : m_vNav)
        {
            pNav->setStyleSheet(strNavStyle);
        }
    }

    void ClientWindow::updateDesktopLibraryLayout()
    {
        const bool bDesktop = !mobileMode();
        const bool bLibrary = bDesktop && m_bUnlocked && m_pPages->currentIndex() == 1 &&
            !m_bReaderFullscreen;
        m_pDesktopShelfView->setVisible(bLibrary);
        m_pDesktopClearSearch->setVisible(bLibrary && !m_strActiveSearch.isEmpty());
        for (auto* pShortcut : m_vDesktopLibraryShortcuts)
        {
            pShortcut->setEnabled(desktopLibraryActive());
        }

        const bool bCompactCards = bDesktop && m_bDesktopShelfList;
        if (m_pCards->property("desktopCompactCards").toBool() != bCompactCards)
        {
            auto* pAnchor = m_pCards->itemAt(7, 7);
            if (auto* pCurrent = m_pCards->currentItem(); pCurrent &&
                m_pCards->viewport()->rect().intersects(m_pCards->visualItemRect(pCurrent)))
            {
                pAnchor = pCurrent;
            }
            m_pCards->setProperty("desktopCompactCards", bCompactCards);
            if (bDesktop)
            {
                m_pCards->doItemsLayout();
                if (pAnchor && bLibrary)
                {
                    m_pCards->scrollToItem(pAnchor, QAbstractItemView::PositionAtTop);
                }
            }
        }
    }

    bool ClientWindow::returnToDesktopLibrary()
    {
        if (mobileMode() || m_oLibraryReturn.m_nBook <= 0)
        {
            return false;
        }
        const auto oReturn = m_oLibraryReturn;
        m_oLibraryReturn = {};
        ++m_nBusinessContext;
        m_pTitle->setText(oReturn.m_strTitle);
        m_pSubtitle->setText(oReturn.m_strSubtitle);
        m_pPages->returnTo(1);
        m_pBodyLayout->activate();
        QListWidgetItem* pSelected = nullptr;
        for (int nRow = 0; nRow < m_pCards->count(); ++nRow)
        {
            auto* pItem = m_pCards->item(nRow);
            if (pItem->data(Qt::UserRole).toInt() == oReturn.m_nBook)
            {
                if (auto oSaved = m_pReadingState->position(oReturn.m_nBook))
                {
                    setLibraryPosition(pItem, *oSaved);
                }
                m_pCards->setCurrentItem(pItem);
                pSelected = pItem;
                break;
            }
        }
        m_pCards->setFocus(Qt::OtherFocusReason);
        m_pCards->doItemsLayout();
        m_pCards->verticalScrollBar()->setValue(oReturn.m_nScroll);
        if (pSelected && !m_pCards->viewport()->rect().intersects(m_pCards->visualItemRect(pSelected)))
        {
            m_pCards->scrollToItem(pSelected, QAbstractItemView::EnsureVisible);
        }
        return true;
    }

    void ClientWindow::setLibraryPosition(QListWidgetItem* pItem, const ReadingEntry& oSaved)
    {
        const int nProgress = qBound(0, qRound(oSaved.m_fProgress * 100), 100);
        pItem->setData(Qt::UserRole + 4, QString("%1 · %2%")
            .arg(oSaved.m_strChapterTitle.isEmpty() ? "已读" : oSaved.m_strChapterTitle)
            .arg(nProgress));
        pItem->setData(Qt::UserRole + 7, nProgress);
        if (m_nSection == 3 && m_strActiveSearch.isEmpty())
        {
            pItem->setData(Qt::UserRole + 2,
                           oSaved.m_oUpdated.toLocalTime().toString("MM-dd HH:mm") + " · 继续阅读");
        }
    }
} // namespace Wenku8::Reader
