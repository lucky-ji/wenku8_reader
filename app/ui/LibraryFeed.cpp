// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#ifdef WENKU8_CLIENT_ONLINE
#include "OnlineSession.h"
#endif
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace Wenku8::Reader
{
    void ClientWindow::wireLibraryFeed()
    {
        m_pDiscoveryLoadTimer = new QTimer(this);
        m_pDiscoveryLoadTimer->setSingleShot(true);
        connect(m_pDiscoveryLoadTimer, &QTimer::timeout, this,
                [this]
                {
                    loadMoreDiscovery();
                });
        const auto fnSchedule = [this]
        {
            if (isDiscoveryFeed() && !m_bLoadingBusy && m_nDiscoveryPage < m_nDiscoveryLastPage &&
                !m_bDiscoveryAppendFailed)
            {
                m_pDiscoveryLoadTimer->start();
            }
        };
        auto* pScroll = m_pCards->verticalScrollBar();
        connect(pScroll, &QScrollBar::valueChanged, this, fnSchedule);
        connect(pScroll, &QScrollBar::rangeChanged, this, fnSchedule);
        connect(m_pPages, &QStackedWidget::currentChanged, this, fnSchedule);
    }

    bool ClientWindow::isDiscoveryFeed() const
    {
        return m_bUnlocked && m_nSection == 1 && m_strActiveSearch.isEmpty() &&
               m_pStartupPages->currentIndex() == 1 && m_pPages->currentIndex() == 1;
    }

    void ClientWindow::resetDiscoveryFeed()
    {
        m_pDiscoveryLoadTimer->stop();
        m_nDiscoveryPage = 0;
        m_nDiscoveryLastPage = 0;
        m_nDiscoveryPendingPage = 0;
        m_bDiscoveryAppendFailed = false;
    }

    void ClientWindow::loadMoreDiscovery(bool bRetry)
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (!isDiscoveryFeed() || !m_pCards->isVisible() || m_bLoadingBusy || !m_pSession ||
            !m_pSession->authenticated() || m_pSession->busy() || m_pCards->count() == 0 ||
            m_nDiscoveryPage >= m_nDiscoveryLastPage || m_bDiscoveryAppendFailed != bRetry)
        {
            return;
        }
        const auto* pScroll = m_pCards->verticalScrollBar();
        if (!bRetry &&
            (pScroll->maximum() - pScroll->value() > 24 ||
             (m_nRetryContext == m_nBusinessContext && m_pSession->hasRetryableRequest())))
        {
            return;
        }
        ++m_nBusinessContext;
        m_nDiscoveryPendingPage = m_nDiscoveryPage + 1;
        m_bDiscoveryAppendFailed = false;
        updateLibraryCount();
        m_pSession->latest(m_nDiscoveryPendingPage, bRetry);
#else
        Q_UNUSED(bRetry);
#endif
    }

    void ClientWindow::finishDiscoveryLoad()
    {
        if (m_nDiscoveryPendingPage == 0)
        {
            return;
        }
        m_bDiscoveryAppendFailed = m_nDiscoveryPage != m_nDiscoveryPendingPage;
        m_nDiscoveryPendingPage = 0;
        updateLibraryCount();
    }

    void ClientWindow::receiveLibraryNovels(int nDiscoveryPage, int nDiscoveryLastPage, bool bStale)
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (!m_bUnlocked || m_pPages->currentIndex() != 1)
        {
            return;
        }
        const auto& vNovels = m_pSession->novels();
        if (nDiscoveryPage == 0)
        {
            if (!isDiscoveryFeed())
            {
                populate(vNovels);
            }
            return;
        }
        if (!isDiscoveryFeed())
        {
            return;
        }
        if (nDiscoveryPage == 1)
        {
            if (bStale && m_nDiscoveryPage > 0)
            {
                return;
            }
            m_nDiscoveryPage = 1;
            m_nDiscoveryLastPage = !bStale && !vNovels.empty() ? qMax(1, nDiscoveryLastPage) : 0;
            m_bDiscoveryAppendFailed = false;
            populate(vNovels);
            m_pCards->doItemsLayout();
            m_pCards->scrollToTop();
        }
        else
        {
            if (bStale || nDiscoveryPage != m_nDiscoveryPendingPage)
            {
                return;
            }
            auto* pScroll = m_pCards->verticalScrollBar();
            const int nScroll = pScroll->value();
            const int nAdded = appendLibraryBooks(vNovels);
            m_nDiscoveryPage = nDiscoveryPage;
            m_nDiscoveryLastPage =
                nAdded > 0 ? qMax(nDiscoveryPage, nDiscoveryLastPage) : nDiscoveryPage;
            m_pCards->doItemsLayout();
            pScroll->setValue(nScroll);
            updateLibraryCount();
        }
        // The queued bottom check runs after busyChanged(false) and after item layout.
        m_pDiscoveryLoadTimer->start();
#else
        Q_UNUSED(nDiscoveryPage);
        Q_UNUSED(nDiscoveryLastPage);
        Q_UNUSED(bStale);
#endif
    }

    void ClientWindow::populate(const std::vector<Novel>& vNovels)
    {
        if (!m_bUnlocked)
        {
            return;
        }
        m_pCards->clear();
        appendLibraryBooks(vNovels);
        updateLibraryCount();
    }

    int ClientWindow::appendLibraryBooks(const std::vector<Novel>& vNovels)
    {
        const bool bDiscovery = isDiscoveryFeed();
        const bool bDownloads = m_nSection == 2 && m_strActiveSearch.isEmpty();
        const int nPreviousCount = m_pCards->count();
        QSet<int> vIds;
        if (bDiscovery)
        {
            for (int nRow = 0; nRow < nPreviousCount; ++nRow)
            {
                vIds.insert(m_pCards->item(nRow)->data(Qt::UserRole).toInt());
            }
        }
        for (const auto& oNovel : vNovels)
        {
            if (bDiscovery && vIds.contains(oNovel.m_nId))
            {
                continue;
            }
            auto* pItem = new QListWidgetItem(QString::fromStdString(oNovel.m_strTitle));
            const QString strAuthor = QString::fromStdString(oNovel.m_strAuthor).simplified();
            QString strStatus = QString::fromStdString(oNovel.m_strStatus).simplified();
            if (strStatus == "连载" || strStatus == "連載" || strStatus == "連載中")
            {
                strStatus = "连载中";
            }
            else if (strStatus == "完成" || strStatus == "已完成" || strStatus == "完结" ||
                     strStatus == "完本" || strStatus == "已完本" || strStatus == "全本" ||
                     strStatus == "完結" || strStatus == "已完結")
            {
                strStatus = "已完结";
            }
            pItem->setData(Qt::UserRole, oNovel.m_nId);
            pItem->setData(Qt::UserRole + 1, strAuthor);
            pItem->setData(Qt::UserRole + 2, bDownloads ? QString("已缓存") : strStatus);
            if (bDiscovery)
            {
                pItem->setData(nLazyCoverRole, true);
                vIds.insert(oNovel.m_nId);
            }
            else
            {
                pItem->setData(Qt::UserRole + 3, m_pCovers->image(oNovel.m_nId));
                pItem->setData(Qt::UserRole + 6, m_pCovers->placeholder(oNovel.m_nId));
            }
            pItem->setData(Qt::UserRole + 5, QString::fromStdString(oNovel.m_strLatestChapter));
            if (auto oSaved = m_pReadingState->position(oNovel.m_nId))
            {
                setLibraryPosition(pItem, *oSaved);
            }
            QStringList vMetadata;
            if (!strAuthor.isEmpty())
            {
                vMetadata.append(QString("作者 · %1").arg(strAuthor));
            }
            if (!strStatus.isEmpty())
            {
                vMetadata.append(QString("连载状态 · %1").arg(strStatus));
            }
            pItem->setData(Qt::AccessibleDescriptionRole, vMetadata.join("，"));
            QString strToolTip = pItem->text().toHtmlEscaped();
            for (const auto& strField : vMetadata)
            {
                strToolTip += "<br>" + strField.toHtmlEscaped();
            }
            pItem->setToolTip("<qt>" + strToolTip + "</qt>");
            m_pCards->addItem(pItem);
            if (!bDiscovery)
            {
                requestCover(oNovel.m_nId);
            }
        }
        return m_pCards->count() - nPreviousCount;
    }

    void ClientWindow::updateLibraryCount()
    {
        const int nCount = m_pCards->count();
        if (nCount == 0)
        {
            const QStringList vEmptyStates{"书架还没有小说，试试搜索或发现页。", "暂时没有小说。",
                                           "还没有缓存小说。", "还没有阅读记录。"};
            m_pCount->setText(m_strActiveSearch.isEmpty() ? vEmptyStates.value(m_nSection)
                                                          : QString("没有找到匹配的小说。"));
            return;
        }
        QString strCount = QString("%1 本小说").arg(nCount);
        if (isDiscoveryFeed())
        {
            if (m_nDiscoveryPendingPage > m_nDiscoveryPage)
            {
                strCount += " · 正在加载更多…";
            }
            else if (m_bDiscoveryAppendFailed)
            {
                strCount += " · 加载更多失败";
            }
            else if (m_nDiscoveryPage > 0 && m_nDiscoveryPage == m_nDiscoveryLastPage)
            {
                strCount += " · 已到底";
            }
        }
        m_pCount->setText(strCount);
    }

    bool ClientWindow::returnToLibrary()
    {
        if (m_oLibraryReturn.m_nBook <= 0)
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
        if (!mobileMode())
        {
            m_pCards->setFocus(Qt::OtherFocusReason);
        }
        m_pCards->doItemsLayout();
        m_pCards->verticalScrollBar()->setValue(oReturn.m_nScroll);
        if (pSelected &&
            !m_pCards->viewport()->rect().intersects(m_pCards->visualItemRect(pSelected)))
        {
            m_pCards->scrollToItem(pSelected, QAbstractItemView::EnsureVisible);
        }
        return true;
    }
} // namespace Wenku8::Reader
