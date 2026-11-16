// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#include "Icons.h"
#include "TouchScroll.h"
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Wenku8::Reader
{
    void ClientWindow::showReaderContents()
    {
        if (!m_bUnlocked || m_bLoadingBusy || m_pPages->currentIndex() != 3 || m_vFlat.isEmpty())
        {
            return;
        }

        const int nBook = m_oBook.m_nId;
        const int nCurrentChapter = m_nChapter;
        const bool bMobile = mobileMode();
        const Colors oStColors = colors(m_eTheme);
        auto* pPage = new QWidget;
        pPage->setObjectName("readerContentsPage");
        pPage->setProperty("readerContents", true);
        auto* pLayout = new QVBoxLayout(pPage);
        pLayout->setContentsMargins(bMobile ? 12 : 24, bMobile ? 12 : 20, bMobile ? 12 : 24,
                                    bMobile ? 12 : 20);
        pLayout->setSpacing(10);

        auto* pTop = new QHBoxLayout;
        auto* pBack = new QPushButton("返回阅读");
        pBack->setObjectName("readerContentsBack");
        pBack->setCursor(Qt::PointingHandCursor);
        pBack->setMinimumHeight(bMobile ? 48 : 40);
        setButtonIcon(pBack, Icon::Left);
        pBack->setVisible(!bMobile);
        pTop->addWidget(pBack);
        auto* pTitle = new QLabel(
            QString::fromUtf8(m_oBook.m_strTitle.data(), int(m_oBook.m_strTitle.size())));
        pTitle->setObjectName("bookTitle");
        pTitle->setTextFormat(Qt::PlainText);
        pTitle->setWordWrap(true);
        pTop->addWidget(pTitle, 1);
        pLayout->addLayout(pTop);

        auto* pCurrent = new QLabel(
            nCurrentChapter >= 0 && nCurrentChapter < m_vFlat.size()
                ? "正在阅读 · " + QString::fromUtf8(m_vFlat[nCurrentChapter].m_strTitle.c_str())
                : "选择章节，继续阅读");
        pCurrent->setObjectName("muted");
        pCurrent->setTextFormat(Qt::PlainText);
        pCurrent->setWordWrap(true);
        pLayout->addWidget(pCurrent);

        auto* pSearchRow = new QHBoxLayout;
        pSearchRow->setSpacing(8);
        auto* pSearch = new QLineEdit;
        pSearch->setObjectName("readerContentsSearch");
        pSearch->setPlaceholderText("搜索章节或卷名");
        pSearch->setClearButtonEnabled(true);
        pSearch->setMinimumHeight(bMobile ? 48 : 40);
        pSearchRow->addWidget(pSearch, 1);
        auto* pLocate = new QPushButton("当前章");
        pLocate->setObjectName("readerContentsLocate");
        pLocate->setMinimumHeight(bMobile ? 48 : 40);
        pLocate->setCursor(Qt::PointingHandCursor);
        pLocate->setProperty("textAction", true);
        if (!bMobile)
        {
            setButtonIcon(pLocate, Icon::List);
        }
        pSearchRow->addWidget(pLocate);
        pLayout->addLayout(pSearchRow);

        auto* pCount = new QLabel(QString("共 %1 章 · 点击章节开始阅读").arg(m_vFlat.size()));
        pCount->setObjectName("muted");
        pCount->setWordWrap(true);
        pLayout->addWidget(pCount);

        auto* pTree = new QTreeWidget;
        pTree->setObjectName("readerContentsTree");
        pTree->setAccessibleName("小说章节目录");
        pTree->setHeaderHidden(true);
        pTree->setColumnCount(1);
        pTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        pTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        pTree->setSelectionMode(QAbstractItemView::SingleSelection);
        pTree->setMouseTracking(true);
        pTree->setIndentation(bMobile ? 16 : 24);
        pTree->setUniformRowHeights(true);
        pLayout->addWidget(pTree, 1);

        QTreeWidgetItem* pCurrentItem = nullptr;
        for (int nVolume = 0; nVolume < m_pToc->topLevelItemCount(); ++nVolume)
        {
            const auto* pSourceVolume = m_pToc->topLevelItem(nVolume);
            auto* pVolume = new QTreeWidgetItem(pTree, {pSourceVolume->text(0)});
            pVolume->setFlags(Qt::ItemIsEnabled);
            pVolume->setSizeHint(0, QSize(0, bMobile ? 48 : 40));
            QFont oStVolumeFont = pTree->font();
            oStVolumeFont.setBold(true);
            pVolume->setFont(0, oStVolumeFont);
            pVolume->setBackground(0, oStColors.m_oSection);
            for (int nChild = 0; nChild < pSourceVolume->childCount(); ++nChild)
            {
                const auto* pSourceChapter = pSourceVolume->child(nChild);
                const QVariant oVIndex = pSourceChapter->data(0, Qt::UserRole);
                const int nIndex = oVIndex.toInt();
                if (!oVIndex.isValid() || nIndex < 0 || nIndex >= m_vFlat.size())
                {
                    continue;
                }
                auto* pChapter = new QTreeWidgetItem(
                    pVolume, {QString::fromUtf8(m_vFlat[nIndex].m_strTitle.c_str())});
                pChapter->setData(0, Qt::UserRole, nIndex);
                pChapter->setSizeHint(0, QSize(0, bMobile ? 48 : 40));
                if (nIndex == nCurrentChapter)
                {
                    pCurrentItem = pChapter;
                    pChapter->setData(0, Qt::UserRole + 1, true);
                    pChapter->setBackground(0, oStColors.m_oSelection);
                    pChapter->setForeground(0, oStColors.m_oSelectionText);
                    QFont oStChapterFont = pTree->font();
                    oStChapterFont.setBold(true);
                    pChapter->setFont(0, oStChapterFont);
                    pChapter->setToolTip(0, "当前阅读章节");
                }
            }
            pVolume->setExpanded(true);
        }

        auto fnActivate = [this, pPage, nBook, bMobile](QTreeWidgetItem* pItem)
        {
            // A mouse double click or an accessibility activation may follow the
            // first click. Once the page has been popped, it cannot switch twice.
            if (!pItem || (bMobile ? m_pPages->currentWidget() != pPage
                         : m_pPages->currentIndex() != 3 || !pPage->isVisible()) || m_bLoadingBusy ||
                m_oBook.m_nId != nBook)
            {
                return;
            }
            const QVariant oVIndex = pItem->data(0, Qt::UserRole);
            if (!oVIndex.isValid())
            {
                pItem->setExpanded(!pItem->isExpanded());
                return;
            }
            const int nIndex = oVIndex.toInt();
            if (nIndex < 0 || nIndex >= m_vFlat.size())
            {
                return;
            }
            const bool bSameChapter = nIndex == m_nChapter;
            if (bMobile)
            {
                popMobilePage();
            }
            if (!bSameChapter)
            {
                m_nRestoreOffset = -1;
                openChapter(nIndex);
            }
        };
        connect(pTree, &QTreeWidget::itemClicked, pPage,
                [fnActivate](QTreeWidgetItem* pItem, int)
                {
                    fnActivate(pItem);
                });
        connect(pTree, &QTreeWidget::itemActivated, pPage,
                [fnActivate](QTreeWidgetItem* pItem, int)
                {
                    fnActivate(pItem);
                });
        auto* pTouch = new TouchScroll(pTree,
                                       [pTree, fnActivate](QPoint oPtPosition)
                                       {
                                           if (auto* pItem = pTree->itemAt(oPtPosition))
                                           {
                                               if (pItem->data(0, Qt::UserRole).isValid())
                                               {
                                                   pTree->setCurrentItem(pItem);
                                               }
                                               fnActivate(pItem);
                                           }
                                       });
        pTouch->setEnabled(bMobile);

        connect(pBack, &QPushButton::clicked, pPage,
                [this, pPage]
                {
                    if (m_pPages->currentWidget() == pPage && !m_bLoadingBusy)
                    {
                        popMobilePage();
                    }
                });
        connect(pSearch, &QLineEdit::textChanged, pPage,
                [pTree, pCount](const QString& strText)
                {
                    const QString strQuery = strText.trimmed();
                    int nMatches = 0;
                    for (int nVolume = 0; nVolume < pTree->topLevelItemCount(); ++nVolume)
                    {
                        auto* pVolume = pTree->topLevelItem(nVolume);
                        const bool bVolumeMatches =
                            pVolume->text(0).contains(strQuery, Qt::CaseInsensitive);
                        bool bAnyVisible = false;
                        for (int nChild = 0; nChild < pVolume->childCount(); ++nChild)
                        {
                            auto* pChapter = pVolume->child(nChild);
                            const bool bMatches =
                                bVolumeMatches ||
                                pChapter->text(0).contains(strQuery, Qt::CaseInsensitive);
                            pChapter->setHidden(!bMatches);
                            if (bMatches)
                            {
                                ++nMatches;
                            }
                            bAnyVisible = bAnyVisible || bMatches;
                        }
                        pVolume->setHidden(!bAnyVisible);
                        if (!strQuery.isEmpty())
                        {
                            pVolume->setExpanded(true);
                        }
                    }
                    pCount->setText(strQuery.isEmpty()
                                        ? QString("共 %1 章 · 点击章节开始阅读").arg(nMatches)
                                    : nMatches == 0 ? "没有匹配的章节，试试其他关键词"
                                                    : QString("找到 %1 个章节").arg(nMatches));
                });
        auto fnLocate = [pTree, pSearch, pCurrentItem]
        {
            if (!pCurrentItem)
            {
                return;
            }
            pSearch->clear();
            if (pCurrentItem->parent())
            {
                pCurrentItem->parent()->setExpanded(true);
            }
            pTree->setCurrentItem(pCurrentItem);
            pTree->scrollToItem(pCurrentItem, QAbstractItemView::PositionAtCenter);
        };
        pLocate->setEnabled(pCurrentItem != nullptr);
        connect(pLocate, &QPushButton::clicked, pPage, fnLocate);

        if (bMobile)
        {
            pushMobilePage(pPage, "目录");
        }
        else
        {
            pBack->hide();
            showReaderPanel(pPage, "目录");
        }
        QTimer::singleShot(0, pPage, fnLocate);
    }
} // namespace Wenku8::Reader
