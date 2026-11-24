// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "PageControls.h"
#include "ReadAloudController.h"
#include "AnimatedStack.h"
#include <QApplication>
#include <QButtonGroup>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <array>
#include <memory>
namespace Wenku8::Reader
{
    using namespace PageControls;

    void ClientWindow::initializeDesktopReaderPanelShortcut()
    {
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        auto* pToggle = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this);
        pToggle->setObjectName("readerSidebarShortcut");
        pToggle->setAutoRepeat(false);
        connect(pToggle, &QShortcut::activated, this, &ClientWindow::toggleDesktopReaderPanel);
#endif
    }

    void ClientWindow::toggleDesktopReaderPanel()
    {
        if (mobileMode() || !canReadBook() || m_bLoadingBusy || m_bRestoring ||
            m_pPages->currentIndex() != 3 ||
            m_nChapter < 0 || m_nChapter >= m_vFlat.size() || QApplication::activePopupWidget())
        {
            return;
        }
        if (m_pReaderDock && m_pReaderDock->isVisible())
        {
            closeReaderPanel();
            return;
        }

        const QString strTitle = m_pReaderDock && m_pReaderDock->property("bookId").toInt() == m_oBook.m_nId
                                     ? m_pReaderDock->property("panelTitle").toString()
                                     : QString();
        if (revealDesktopReaderPanel(strTitle))
        {
            return;
        }
        if (strTitle == "章内查找")
        {
            showReaderSearch();
        }
        else if (strTitle == "书签")
        {
            showDesktopBookmarks();
        }
        else if (strTitle == "听书")
        {
            showReadAloud();
        }
        else
        {
            showReaderContents();
        }
    }

    bool ClientWindow::revealDesktopReaderPanel(const QString& strTitle)
    {
        if (strTitle.isEmpty() || mobileMode() || !canReadBook() || m_bLoadingBusy ||
            m_bRestoring || m_pPages->currentIndex() != 3 || !m_pReaderDock ||
            !m_pReaderDock->widget() || m_nChapter < 0 || m_nChapter >= m_vFlat.size() ||
            m_pReaderDock->property("bookId").toInt() != m_oBook.m_nId ||
            m_pReaderDock->property("panelChapter").toInt() != m_vFlat[m_nChapter].m_nId ||
            m_pReaderDock->property("panelTitle").toString() != strTitle)
        {
            return false;
        }
        m_bReaderDockRequested = true;
        m_pReaderDock->show();
        auto* pBody = m_pReaderDock->widget()->findChild<QStackedWidget*>("readerPanelBody");
        if (auto* pSearch = pBody->currentWidget()->findChild<QLineEdit*>("readerSearchInput"))
        {
            pSearch->setFocus(Qt::ShortcutFocusReason);
            pSearch->selectAll();
        }
        return true;
    }

    void ClientWindow::closeReaderPanel()
    {
        m_bReaderDockRequested = false;
        if (m_pReaderDock)
        {
            m_pReaderDock->hide();
        }
        if (m_pPages->currentIndex() == 3)
        {
            m_pReader->setFocus();
        }
    }
    void ClientWindow::showReaderPanel(QWidget* pPage, const QString& strTitle)
    {
        if (!pPage)
        {
            return;
        }
        if (mobileMode() || !canReadBook() || m_bLoadingBusy || m_bRestoring ||
            m_pPages->currentIndex() != 3 || m_oBook.m_nId <= 0 ||
            m_nChapter < 0 || m_nChapter >= m_vFlat.size())
        {
            pPage->deleteLater();
            return;
        }
        struct ReaderTab
        {
            QString m_strTitle, m_strLabel;
            Icon m_eIcon;
            void (ClientWindow::*m_fnOpen)();
        };
        static const std::array<ReaderTab, 4> vTabs{{
            {"目录", "目录", Icon::List, &ClientWindow::showReaderContents},
            {"章内查找", "查找", Icon::Search, &ClientWindow::showReaderSearch},
            {"书签", "书签", Icon::Bookmark, &ClientWindow::showDesktopBookmarks},
            {"听书", "听书", Icon::Headphones, &ClientWindow::showReadAloud},
        }};
        const bool bFirstOpen = !m_pReaderDock;
        if (bFirstOpen)
        {
            m_pReaderDock = new QDockWidget("阅读侧栏", this);
            m_pReaderDock->setObjectName("desktopReaderDock");
            m_pReaderDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
            m_pReaderDock->setFeatures(QDockWidget::DockWidgetClosable |
                                       QDockWidget::DockWidgetMovable);
            m_pReaderDock->setMinimumWidth(292);
            addDockWidget(Qt::RightDockWidgetArea, m_pReaderDock);
            connect(m_pReaderDock, &QDockWidget::visibilityChanged, this,
                    [this](bool bVisible)
                    {
                        if (!m_bAdaptingLayout && !mobileMode() && m_pPages->currentIndex() == 3 &&
                            (bVisible || m_pReaderDock->isHidden()))
                        {
                            m_bReaderDockRequested = bVisible;
                        }
                    });
            auto* pHost = new QWidget;
            auto* pLayout = new QVBoxLayout(pHost);
            pLayout->setContentsMargins(8, 8, 8, 8);
            pLayout->setSpacing(8);
            auto* pGroup = new QWidget;
            pGroup->setObjectName("controlGroup");
            auto* pTabs = new QHBoxLayout(pGroup);
            pTabs->setContentsMargins(4, 4, 4, 4);
            pTabs->setSpacing(4);
            auto* pTabGroup = new QButtonGroup(pHost);
            pTabGroup->setObjectName("readerPanelTabs");
            for (int nIndex = 0; nIndex < int(vTabs.size()); ++nIndex)
            {
                const auto& oTab = vTabs[nIndex];
                auto* pAction = new QPushButton(oTab.m_strLabel);
                pAction->setObjectName("readerPanelTab" + QString::number(nIndex));
                pAction->setAccessibleName(oTab.m_strTitle);
                pAction->setToolTip(oTab.m_strTitle);
                pAction->setCursor(Qt::PointingHandCursor);
                pAction->setProperty("settingsCompactAction", true);
                pAction->setCheckable(true);
                pAction->setMinimumHeight(36);
                setButtonIcon(pAction, oTab.m_eIcon);
                pTabs->addWidget(pAction, 1);
                pTabGroup->addButton(pAction, nIndex);
                connect(pAction, &QPushButton::clicked, pHost,
                        [this, pTabGroup, nIndex]
                        {
                            if (canReadBook() && !m_bLoadingBusy && !m_bRestoring && !mobileMode() &&
                                m_pPages->currentIndex() == 3 && m_pReaderDock->isVisible() &&
                                m_pReaderDock->property("bookId").toInt() == m_oBook.m_nId &&
                                m_pReaderDock->property("panelTitle") != vTabs[nIndex].m_strTitle)
                            {
                                (this->*vTabs[nIndex].m_fnOpen)();
                            }
                            // A request can become stale before its page opens. Reflect
                            // the visible page regardless of which button was pressed.
                            for (int nTab = 0; nTab < int(vTabs.size()); ++nTab)
                            {
                                if (m_pReaderDock->property("panelTitle") == vTabs[nTab].m_strTitle)
                                {
                                    pTabGroup->button(nTab)->setChecked(true);
                                    break;
                                }
                            }
                        });
            }
            pLayout->addWidget(pGroup);
            auto* pBody = new QStackedWidget;
            pBody->setObjectName("readerPanelBody");
            pLayout->addWidget(pBody, 1);
            m_pReaderDock->setWidget(pHost);
        }
        auto* pHost = m_pReaderDock->widget();
        auto* pBody = pHost->findChild<QStackedWidget*>("readerPanelBody");
        if (auto* pOld = pBody->currentWidget())
        {
            pBody->removeWidget(pOld);
            pOld->setParent(this);
            pOld->deleteLater();
        }
        pBody->addWidget(pPage);
        m_pReaderDock->setProperty("bookId", m_oBook.m_nId);
        m_pReaderDock->setProperty("panelChapter", m_vFlat[m_nChapter].m_nId);
        m_pReaderDock->setProperty("panelTitle", strTitle);
        auto* pTabGroup = pHost->findChild<QButtonGroup*>("readerPanelTabs");
        for (int nIndex = 0; nIndex < int(vTabs.size()); ++nIndex)
        {
            if (vTabs[nIndex].m_strTitle == strTitle)
            {
                pTabGroup->button(nIndex)->setChecked(true);
                break;
            }
        }
        m_bReaderDockRequested = true;
        applyTheme(pHost, m_eTheme);
        m_pReaderDock->show();
        if (bFirstOpen)
        {
            resizeDocks({m_pReaderDock}, {348}, Qt::Horizontal);
        }
    }
    void ClientWindow::showDesktopBookmarks()
    {
        if (mobileMode() || !canReadBook() || m_bLoadingBusy || m_nChapter < 0 ||
            m_nChapter >= m_vFlat.size() || m_oBook.m_nId <= 0 || m_pPages->currentIndex() != 3)
        {
            return;
        }
        const int nBook = m_oBook.m_nId;
        auto* pPage = new QWidget;
        auto* pLayout = new QVBoxLayout(pPage);
        auto* pAdd = new QPushButton("添加当前位置（Ctrl+D）");
        pAdd->setObjectName("desktopBookmarkAdd");
        pLayout->addWidget(pAdd);
        auto* pEmpty = new QLabel("还没有书签，点击上方「添加当前位置」保存当前阅读位置。");
        pEmpty->setObjectName("desktopBookmarksEmpty");
        pEmpty->setWordWrap(true);
        pEmpty->setAlignment(Qt::AlignCenter);
        pEmpty->setMinimumHeight(96);
        pLayout->addWidget(pEmpty, 1);
        auto* pList = new QListWidget;
        pList->setObjectName("bookmarkItems");
        pList->setWordWrap(true);
        pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pList->setToolTip("双击书签或选中后按 Enter，跳转到保存的位置");
        pLayout->addWidget(pList, 1);
        auto* pRemove = new QPushButton("删除选中书签");
        pRemove->setObjectName("removeBookmark");
        auto* pUndo = new QPushButton("撤销删除");
        pUndo->setObjectName("undoDesktopBookmark");
        pUndo->hide();
        pLayout->addWidget(pRemove);
        pLayout->addWidget(pUndo);
        auto pEntries = std::make_shared<QVector<BookmarkEntry>>();
        auto pDeleted = std::make_shared<std::optional<BookmarkEntry>>();
        auto* pUndoTimer = new QTimer(pPage);
        pUndoTimer->setSingleShot(true);
        auto fnCurrentPage = [this, pPage, nBook]
        {
            return canReadBook() && !m_bLoadingBusy && !m_bRestoring && !mobileMode() &&
                   m_pPages->currentIndex() == 3 && m_oBook.m_nId == nBook && m_pReaderDock &&
                   pPage->isVisible() && m_pReaderDock->isVisible() && m_pReaderDock->widget() &&
                   m_pReaderDock->widget()->isAncestorOf(pPage);
        };
        auto fnReload = [this, pEntries, pList, pEmpty, pRemove, nBook]
        {
            *pEntries = m_pReadingState->bookmarks(nBook);
            pList->clear();
            for (const auto& oEntry : *pEntries)
            {
                auto* pItem =
                    new QListWidgetItem(oEntry.m_strTitle + "\n" + oEntry.m_strSnippet, pList);
                pItem->setSizeHint(QSize(200, 104));
            }
            pEmpty->setVisible(pEntries->isEmpty());
            pList->setVisible(!pEntries->isEmpty());
            pRemove->setEnabled(false);
        };
        connect(pAdd, &QPushButton::clicked, pPage,
                [this, fnCurrentPage, fnReload]
                {
                    if (!fnCurrentPage())
                    {
                        return;
                    }
                    addBookmark();
                    fnReload();
                });
        connect(pList, &QListWidget::currentRowChanged, pRemove,
                [pRemove, pEntries, fnCurrentPage](int nRow)
                {
                    pRemove->setEnabled(fnCurrentPage() && nRow >= 0 && nRow < pEntries->size());
                });
        // Qt emits itemActivated for the desktop activation gesture and keyboard Enter.
        connect(pList, &QListWidget::itemActivated, pPage,
                [this, pEntries, pList, fnCurrentPage](QListWidgetItem* pItem)
                {
                    const int nRow = pList->row(pItem);
                    if (!fnCurrentPage() || nRow < 0 || nRow >= pEntries->size())
                    {
                        return;
                    }
                    const auto oEntry = pEntries->at(nRow);
                    if (m_pReadAloud)
                    {
                        m_pReadAloud->setFollow(false);
                    }
                    for (int nIndex = 0; nIndex < m_vFlat.size(); ++nIndex)
                    {
                        if (m_vFlat[nIndex].m_nId != oEntry.m_nChapter)
                        {
                            continue;
                        }
                        if (nIndex == m_nChapter)
                        {
                            m_pReader->restorePosition(oEntry.m_nOffset);
                            updateReaderFooter();
                            saveProgress();
                        }
                        else
                        {
                            m_nRestoreOffset = oEntry.m_nOffset;
                            openChapter(nIndex);
                        }
                        break;
                    }
                });
        connect(pRemove, &QPushButton::clicked, pPage,
                [this, pEntries, pDeleted, pList, pUndo, pUndoTimer, fnReload, fnCurrentPage, nBook]
                {
                    const int nRow = pList->currentRow();
                    if (!fnCurrentPage() || nRow < 0 || nRow >= pEntries->size())
                    {
                        return;
                    }
                    *pDeleted = pEntries->at(nRow);
                    m_pReadingState->removeBookmark(nBook, pDeleted->value().m_strId);
                    fnReload();
                    pUndo->show();
                    pUndoTimer->start(5000);
                });
        connect(pUndo, &QPushButton::clicked, pPage,
                [this, pDeleted, pUndo, pUndoTimer, fnReload, fnCurrentPage, nBook]
                {
                    if (!fnCurrentPage() || !pDeleted->has_value())
                    {
                        return;
                    }
                    m_pReadingState->addBookmark(nBook, pDeleted->value());
                    pDeleted->reset();
                    pUndoTimer->stop();
                    pUndo->hide();
                    fnReload();
                });
        connect(pUndoTimer, &QTimer::timeout, pPage,
                [pDeleted, pUndo]
                {
                    pDeleted->reset();
                    pUndo->hide();
                });
        fnReload();
        showReaderPanel(pPage, "书签");
    }
} // namespace Wenku8::Reader
