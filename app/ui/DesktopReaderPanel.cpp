// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "PageControls.h"
#include "AnimatedStack.h"
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QTimer>
#include <memory>
namespace Wenku8::Reader
{
    using namespace PageControls;
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
        if (mobileMode() || !m_bUnlocked || m_bLoadingBusy ||
            m_pPages->currentIndex() != 3 || m_oBook.m_nId <= 0)
        {
            pPage->deleteLater();
            return;
        }
        if (!m_pReaderDock)
        {
            m_pReaderDock = new QDockWidget("阅读工具", this);
            m_pReaderDock->setObjectName("desktopReaderDock");
            m_pReaderDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
            m_pReaderDock->setFeatures(QDockWidget::DockWidgetClosable |
                                       QDockWidget::DockWidgetMovable);
            m_pReaderDock->setMinimumWidth(280);
            addDockWidget(Qt::RightDockWidgetArea, m_pReaderDock);
            connect(m_pReaderDock, &QDockWidget::visibilityChanged, this,
                    [this](bool bVisible)
                    {
                        if (!m_bAdaptingLayout && !mobileMode() && m_pPages->currentIndex() == 3)
                        {
                            m_bReaderDockRequested = bVisible;
                        }
                    });
        }
        const int nBook = m_oBook.m_nId;
        auto* pHost = new QWidget;
        auto* pLayout = new QVBoxLayout(pHost);
        pLayout->setContentsMargins(8, 8, 8, 8);
        auto* pTabs = new QHBoxLayout;
        const QStringList vTitles{"目录", "章内查找", "书签"};
        for (int nIndex = 0; nIndex < vTitles.size(); ++nIndex)
        {
            auto* pAction = new QPushButton(nIndex == 1 ? "查找" : vTitles[nIndex]);
            pAction->setObjectName("readerPanelTab" + QString::number(nIndex));
            pAction->setCheckable(true);
            pAction->setChecked(vTitles[nIndex] == strTitle);
            pAction->setMinimumHeight(36);
            pTabs->addWidget(pAction);
            connect(pAction, &QPushButton::clicked, pHost,
                    [this, pHost, nBook, nIndex]
                    {
                        if (!m_bUnlocked || m_bLoadingBusy || m_bRestoring || mobileMode() ||
                            m_pPages->currentIndex() != 3 || m_oBook.m_nId != nBook ||
                            !m_pReaderDock || m_pReaderDock->widget() != pHost)
                        {
                            return;
                        }
                        if (nIndex == 0)
                        {
                            showReaderContents();
                        }
                        else if (nIndex == 1)
                        {
                            showReaderSearch();
                        }
                        else
                        {
                            showDesktopBookmarks();
                        }
                    });
        }
        pLayout->addLayout(pTabs);
        pLayout->addWidget(pPage, 1);
        QPointer<QWidget> pOld = m_pReaderDock->widget();
        m_pReaderDock->setWidget(pHost);
        if (pOld)
        {
            pOld->hide();
            pOld->deleteLater();
        }
        m_pReaderDock->setProperty("bookId", nBook);
        m_pReaderDock->setProperty("panelTitle", strTitle);
        m_bReaderDockRequested = true;
        applyTheme(pHost, m_eTheme);
        m_pReaderDock->show();
        resizeDocks({m_pReaderDock}, {340}, Qt::Horizontal);
    }
    void ClientWindow::showDesktopBookmarks()
    {
        if (mobileMode() || !m_bUnlocked || m_bLoadingBusy || m_nChapter < 0 ||
            m_nChapter >= m_vFlat.size() || m_oBook.m_nId <= 0 ||
            m_pPages->currentIndex() != 3)
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
            return m_bUnlocked && !m_bLoadingBusy && !m_bRestoring && !mobileMode() &&
                   m_pPages->currentIndex() == 3 && m_oBook.m_nId == nBook &&
                   m_pReaderDock && m_pReaderDock->isVisible() && m_pReaderDock->widget() &&
                   m_pReaderDock->widget()->isAncestorOf(pPage);
        };
        auto fnReload = [this, pEntries, pList, pEmpty, pRemove, nBook]
        {
            *pEntries = m_pReadingState->bookmarks(nBook);
            pList->clear();
            for (const auto& oEntry : *pEntries)
            {
                auto* pItem = new QListWidgetItem(oEntry.m_strTitle + "\n" + oEntry.m_strSnippet,
                                                 pList);
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
}
