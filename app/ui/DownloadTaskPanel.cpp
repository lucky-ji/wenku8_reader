// SPDX-License-Identifier: GPL-2.0-only
#include "DownloadTaskPanel.h"
#include "DownloadManager.h"
#include "TouchScroll.h"
#include "Icons.h"
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSet>
#include <algorithm>
namespace Wenku8::Reader
{
    DownloadTaskPanel::DownloadTaskPanel(DownloadManager* pManager, QWidget* pParent)
        : QWidget(pParent), m_pManager(pManager)
    {
        setObjectName("downloadTaskPanel");
        auto* pOuter = new QVBoxLayout(this);
        pOuter->setContentsMargins(0, 0, 0, 0);
        auto* pTitle = new QLabel("后台任务");
        pTitle->setObjectName("eyebrow");
        pOuter->addWidget(pTitle);
        m_pEmpty = new QLabel("暂无下载任务。在小说详情选择「下载缓存」或「下载到本地」即可添加。");
        m_pEmpty->setWordWrap(true);
        m_pEmpty->setObjectName("muted");
        pOuter->addWidget(m_pEmpty);
        auto* pScroll = new QScrollArea;
        pScroll->setObjectName("downloadTaskScroll");
        pScroll->setFrameShape(QFrame::NoFrame);
        pScroll->setWidgetResizable(true);
        pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pScroll->setMinimumHeight(150);
        pScroll->setMaximumHeight(240);
        auto* pBody = new QWidget;
        m_pRows = new QVBoxLayout(pBody);
        m_pRows->setContentsMargins(0, 0, 8, 0);
        m_pRows->setSpacing(10);
        m_pRows->addStretch();
        pScroll->setWidget(pBody);
        pOuter->addWidget(pScroll);
        auto* pTouch = new TouchScroll(pScroll);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        pTouch->setEnabled(true);
#else
        pTouch->setEnabled(false);
#endif
        connect(m_pManager, &DownloadManager::tasksChanged, this, &DownloadTaskPanel::refresh);
        refresh();
    }
    void DownloadTaskPanel::refresh()
    {
        auto vTasks = m_pManager->tasks();
        std::stable_sort(vTasks.begin(), vTasks.end(), [](const DownloadTask& oLeft, const DownloadTask& oRight)
        {
            return oLeft.m_eState != DownloadTaskState::Complete && oRight.m_eState == DownloadTaskState::Complete;
        });
        int nRowIndex = 0;
        QSet<QString> vIds;
        m_pEmpty->setVisible(vTasks.isEmpty());
        findChild<QScrollArea*>("downloadTaskScroll")->setVisible(!vTasks.isEmpty());
        for (const auto& oTask : vTasks)
        {
            vIds.insert(oTask.m_strId);
            if (!m_vRows.contains(oTask.m_strId))
            {
                Row oRow;
                oRow.m_pWidget = new QWidget;
                oRow.m_pWidget->setObjectName("downloadTaskRow");
                auto* pLayout = new QVBoxLayout(oRow.m_pWidget);
                pLayout->setContentsMargins(8, 8, 8, 8);
                pLayout->setSpacing(6);
                oRow.m_pTitle = new QLabel;
                oRow.m_pTitle->setTextFormat(Qt::PlainText);
                oRow.m_pTitle->setWordWrap(true);
                pLayout->addWidget(oRow.m_pTitle);
                oRow.m_pProgress = new QProgressBar;
                oRow.m_pProgress->setFixedHeight(8);
                oRow.m_pProgress->setTextVisible(false);
                pLayout->addWidget(oRow.m_pProgress);
                oRow.m_pStatus = new QLabel;
                oRow.m_pStatus->setObjectName("muted");
                oRow.m_pStatus->setTextFormat(Qt::PlainText);
                oRow.m_pStatus->setWordWrap(true);
                pLayout->addWidget(oRow.m_pStatus);
                auto* pActions = new QHBoxLayout;
                oRow.m_pPause = new QPushButton;
                oRow.m_pRemove = new QPushButton("移除记录");
                for (auto* pButton : {oRow.m_pPause, oRow.m_pRemove})
                {
                    pButton->setMinimumHeight(38);
                    pActions->addWidget(pButton);
                }
                pActions->addStretch();
                pLayout->addLayout(pActions);
                const QString strId = oTask.m_strId;
                connect(oRow.m_pPause, &QPushButton::clicked, this, [this, strId]
                {
                    for (const auto& oCurrent : m_pManager->tasks())
                    {
                        if (oCurrent.m_strId == strId)
                        {
                            if (oCurrent.m_eState == DownloadTaskState::Running ||
                                oCurrent.m_eState == DownloadTaskState::Queued)
                            {
                                m_pManager->pause(strId);
                            }
                            else
                            {
                                m_pManager->resume(strId);
                            }
                            break;
                        }
                    }
                });
                connect(oRow.m_pRemove, &QPushButton::clicked, this, [this, strId]
                {
                    m_pManager->remove(strId);
                });
                m_pRows->insertWidget(m_pRows->count() - 1, oRow.m_pWidget);
                m_vRows.insert(strId, oRow);
            }
            auto& oRow = m_vRows[oTask.m_strId];
            if (m_pRows->indexOf(oRow.m_pWidget) != nRowIndex)
            {
                m_pRows->insertWidget(nRowIndex, oRow.m_pWidget);
            }
            ++nRowIndex;
            oRow.m_pTitle->setText((oTask.m_bLocal ? "下载到本地 · " : "下载缓存 · ") + oTask.m_strTitle);
            const bool bComplete = oTask.m_eState == DownloadTaskState::Complete;
            const bool bActive = oTask.m_eState == DownloadTaskState::Running ||
                                 oTask.m_eState == DownloadTaskState::Queued;
            QString strState;
            switch (oTask.m_eState)
            {
            case DownloadTaskState::Queued: strState = "等待下载"; break;
            case DownloadTaskState::Running: strState = "正在下载"; break;
            case DownloadTaskState::Paused: strState = "已暂停"; break;
            case DownloadTaskState::Failed: strState = "等待重试"; break;
            case DownloadTaskState::Complete: strState = "已完成"; break;
            }
            const qint64 nDone = oTask.m_nChaptersCompleted + oTask.m_nImagesCompleted;
            const qint64 nTotal = oTask.m_nChaptersTotal + oTask.m_nImagesTotal;
            if (nTotal > 0)
            {
                oRow.m_pProgress->setRange(0, 1000);
                oRow.m_pProgress->setValue(int(qMin<qint64>(1000, nDone * 1000 / nTotal)));
            }
            else
            {
                oRow.m_pProgress->setRange(0, bComplete || !bActive ? 1 : 0);
                oRow.m_pProgress->setValue(bComplete ? 1 : 0);
            }
            QString strStatus = strState + QString(" · 章节 %1/%2 · 插图 %3/%4")
                .arg(oTask.m_nChaptersCompleted).arg(oTask.m_nChaptersTotal)
                .arg(oTask.m_nImagesCompleted).arg(oTask.m_nImagesTotal);
            if (!oTask.m_strMessage.isEmpty())
            {
                strStatus += "\n" + oTask.m_strMessage;
            }
            oRow.m_pStatus->setText(strStatus);
            oRow.m_pPause->setVisible(!bComplete);
            oRow.m_pPause->setText(bActive ? "暂停" : "继续下载");
            oRow.m_pRemove->setEnabled(!bActive);
            oRow.m_pRemove->setToolTip("仅移除任务记录与本任务的临时文件，不删除已完成的缓存或本地小说。");
        }
        for (auto oIt = m_vRows.begin(); oIt != m_vRows.end();)
        {
            if (!vIds.contains(oIt.key()))
            {
                delete oIt.value().m_pWidget;
                oIt = m_vRows.erase(oIt);
            }
            else
            {
                ++oIt;
            }
        }
    }
}
