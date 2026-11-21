// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "DownloadOption.h"
#include <QBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>

namespace Wenku8::Reader
{
    void ClientWindow::updateBookDetailProgress()
    {
        const auto oSaved = m_pReadingState->position(m_oBook.m_nId);
        const auto oChapter = std::find_if(m_vFlat.cbegin(), m_vFlat.cend(),
                                           [&oSaved](const Chapter& oEntry)
                                           {
                                               return oSaved && oEntry.m_nId == oSaved->m_nChapter;
                                           });
        const bool bCanResume = oChapter != m_vFlat.cend();
        m_pRead->setText(bCanResume ? "继续阅读" : "开始阅读");
        m_pDetailProgressText->setVisible(bCanResume);
        m_pDetailProgress->setVisible(bCanResume);
        if (!bCanResume)
        {
            m_pRead->setToolTip({});
            return;
        }
        const auto& strChapterTitle = oChapter->m_strTitle;
        const QString strChapter =
            QString::fromUtf8(strChapterTitle.data(), int(strChapterTitle.size()));
        const double fProgress = std::isfinite(oSaved->m_fProgress) ? oSaved->m_fProgress : 0.0;
        const int nPercent = qRound(qBound(0.0, fProgress, 1.0) * 100);
        m_pDetailProgress->setValue(nPercent);
        m_pDetailProgressText->setText(QString("本章已读 %1% · %2").arg(nPercent).arg(strChapter));
        m_pDetailProgressText->setToolTip(m_pDetailProgressText->text());
        m_pRead->setToolTip(strChapter);
    }

    void ClientWindow::updateBookDetailFilter()
    {
        const QString strFilter = m_pDetailFilter->text().trimmed();
        const bool bFiltering = !strFilter.isEmpty();
        if (!m_bDetailFiltering && !bFiltering)
        {
            m_pDetailChapterHeading->setText(QString("章节目录 · %1 章").arg(m_vFlat.size()));
            return;
        }
        constexpr int nExpandedBeforeFilterRole = Qt::UserRole + 2;
        const bool bUpdates = m_pToc->updatesEnabled();
        m_pToc->setUpdatesEnabled(false);
        int nMatches = 0;
        for (int nVolume = 0; nVolume < m_pToc->topLevelItemCount(); ++nVolume)
        {
            auto* pVolume = m_pToc->topLevelItem(nVolume);
            if (bFiltering && !m_bDetailFiltering)
            {
                pVolume->setData(0, nExpandedBeforeFilterRole, pVolume->isExpanded());
            }
            const bool bVolumeMatch = pVolume->text(0).contains(strFilter, Qt::CaseInsensitive);
            bool bHasMatch = false;
            for (int nRow = 0; nRow < pVolume->childCount(); ++nRow)
            {
                auto* pChapter = pVolume->child(nRow);
                const bool bMatch = !bFiltering || bVolumeMatch ||
                                    pChapter->text(0).contains(strFilter, Qt::CaseInsensitive);
                pChapter->setHidden(!bMatch);
                if (bMatch)
                {
                    bHasMatch = true;
                    ++nMatches;
                }
            }
            pVolume->setHidden(bFiltering && !bHasMatch);
            if (bFiltering && bHasMatch)
            {
                pVolume->setExpanded(true);
            }
            else if (!bFiltering)
            {
                pVolume->setExpanded(pVolume->data(0, nExpandedBeforeFilterRole).toBool());
            }
        }
        m_pToc->setUpdatesEnabled(bUpdates);
        m_bDetailFiltering = bFiltering;
        if (bFiltering && nMatches == 0)
        {
            m_pDetailChapterHeading->setText("没有匹配章节");
        }
        else
        {
            m_pDetailChapterHeading->setText(bFiltering
                                                 ? QString("匹配 %1 章").arg(nMatches)
                                                 : QString("章节目录 · %1 章").arg(nMatches));
        }
    }

    void ClientWindow::updateBookDetailLayout(bool bMobile, bool bLandscape)
    {
        const bool bSplit = bMobile ? bLandscape && width() >= 620 : width() >= 1160;
        const auto eDirection = bSplit ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (m_pDetailColumns->direction() != eDirection)
        {
            m_pDetailColumns->setDirection(eDirection);
            m_pDetailColumns->setStretch(0, bSplit ? 2 : 0);
            m_pDetailColumns->setStretch(1, bSplit ? 3 : 1);
        }
        m_pDetailColumns->setSpacing(bMobile ? 8 : 16);
        const int nInset = bMobile ? 8 : 18;
        m_pDetailLayout->setContentsMargins(nInset, nInset, nInset, nInset);
        m_pDetailLayout->setSpacing(bMobile ? 6 : 12);
        m_pDetailHeaderLayout->setSpacing(bMobile ? 10 : 14);
        m_pDetailHeaderLayout->setContentsMargins(10, 10, 10, 10);
        const QSize oCoverSize = bMobile ? (bLandscape ? QSize(56, 80) : QSize(76, 108))
                                         : (bSplit ? QSize(92, 132) : QSize(108, 154));
        if (m_pDetailCover->size() != oCoverSize)
        {
            m_pDetailCover->setFixedSize(oCoverSize);
            const auto oImage = m_pCovers->image(m_oBook.m_nId);
            if (!oImage.isNull())
            {
                m_pDetailCover->setPixmap(coverPixmap(oImage, oCoverSize - QSize(4, 6),
                                                      m_pDetailCover->devicePixelRatioF()));
            }
        }
        m_pDetailIntro->setVisible(!bMobile && !m_pDetailIntro->text().isEmpty());
        m_pDetailFilter->setMinimumHeight(bMobile ? 44 : 36);

        auto* pHero = m_pDetailTitle->parentWidget();
        const bool bTwoActionColumns = bMobile || bSplit;
        if (!pHero->property("twoDetailActionColumns").isValid() ||
            pHero->property("twoDetailActionColumns").toBool() != bTwoActionColumns)
        {
            pHero->setProperty("twoDetailActionColumns", bTwoActionColumns);
            while (auto* pItem = m_pDetailActions->takeAt(0))
            {
                delete pItem;
            }
            auto* pSynopsis = findChild<QPushButton*>("showSynopsis");
            m_pDetailActions->addWidget(m_pRead, 0, 0);
            m_pDetailActions->addWidget(m_pShelfAdd, 0, 1);
            m_pDetailActions->addWidget(m_pDownload, 1, 0);
            m_pDetailActions->addWidget(m_pLocalDownload, 1, 1);
            m_pDetailActions->addWidget(m_pIllustrations, bTwoActionColumns ? 2 : 1,
                                        bTwoActionColumns ? 0 : 2);
            m_pDetailActions->addWidget(pSynopsis, bTwoActionColumns ? 2 : 0,
                                        bTwoActionColumns ? 1 : 2);
            for (int nColumn = 0; nColumn < 3; ++nColumn)
            {
                m_pDetailActions->setColumnStretch(nColumn,
                                                   nColumn == 2 && bTwoActionColumns ? 0 : 1);
            }
        }
        const auto oColors = colors(m_eTheme);
        auto fnSetStyle = [](QWidget* pWidget, const QString& strStyle)
        {
            if (pWidget->styleSheet() != strStyle)
            {
                pWidget->setStyleSheet(strStyle);
            }
        };
        fnSetStyle(
            pHero,
            QString(
                "QWidget#bookDetailHero {background:%1; border:1px solid %2; border-radius:14px}")
                .arg(oColors.m_oBackground.name(), oColors.m_oBorder.name()));
        fnSetStyle(m_pDetailTitle, QString("font-size:%1px;font-weight:600;color:%2;")
                                       .arg(bMobile ? 18 : 23)
                                       .arg(oColors.m_oText.name()));
        fnSetStyle(m_pDetailProgressText,
                   QString("font-size:12px;color:%1;").arg(oColors.m_oAccent.name()));
        fnSetStyle(m_pDetailProgress,
                   QString("QProgressBar {background:%1;border:0;border-radius:2px} "
                           "QProgressBar::chunk {background:%2;border-radius:2px}")
                       .arg(oColors.m_oSection.name(), oColors.m_oAccent.name()));
        fnSetStyle(m_pDetailChapterHeading,
                   QString("font-size:14px;font-weight:600;color:%1;").arg(oColors.m_oText.name()));
    }
} // namespace Wenku8::Reader
