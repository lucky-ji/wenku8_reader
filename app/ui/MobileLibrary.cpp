// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#include "Icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace Wenku8::Reader
{
    class MobileContinueCard final : public QPushButton
    {
      public:
        MobileContinueCard()
        {
            setObjectName("mobileContinueReading");
            setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            setFixedHeight(108);
            setCursor(Qt::PointingHandCursor);
            setAccessibleName("继续阅读");
        }

        int book() const
        {
            return m_oEntry.m_nNovel;
        }

        void setBook(const ReadingEntry& oEntry, const QImage& oCover)
        {
            m_oEntry = oEntry;
            m_oCover = oCover;
            setAccessibleName("继续阅读 " + oEntry.m_strTitle);
            setToolTip(oEntry.m_strTitle + "\n" + oEntry.m_strChapterTitle);
            update();
        }

        void setCover(const QImage& oCover)
        {
            m_oCover = oCover;
            update();
        }

        void setTheme(Theme eTheme)
        {
            m_eTheme = eTheme;
            update();
        }

      protected:
        void paintEvent(QPaintEvent*) override
        {
            const auto oColors = colors(m_eTheme);
            QPainter oPainter(this);
            oPainter.setRenderHint(QPainter::Antialiasing);
            oPainter.setPen(QPen(hasFocus() ? oColors.m_oSelectionText : oColors.m_oBorder, 1));
            oPainter.setBrush(isDown() ? oColors.m_oHover : oColors.m_oPanel);
            oPainter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 16, 16);

            const QRect oCover(12, 12, 56, height() - 24);
            QPainterPath oClip;
            oClip.addRoundedRect(oCover, 7, 7);
            oPainter.save();
            oPainter.setClipPath(oClip);
            oPainter.fillRect(oCover, oColors.m_oSection);
            if (m_oCover.isNull())
            {
                makeIcon(Icon::Book, oColors.m_oMuted, oColors.m_oDisabled, oColors.m_oAccent)
                    .paint(&oPainter, oCover.adjusted(16, 30, -16, -30));
            }
            else
            {
                auto oScaled = m_oCover.size();
                oScaled.scale(oCover.size(), Qt::KeepAspectRatioByExpanding);
                QRect oTarget(QPoint{}, oScaled);
                oTarget.moveCenter(oCover.center());
                oPainter.setRenderHint(QPainter::SmoothPixmapTransform);
                oPainter.drawImage(oTarget, m_oCover);
            }
            oPainter.restore();

            const int nTextLeft = oCover.right() + 13;
            const int nTextWidth = width() - nTextLeft - 42;
            auto fnDrawText = [&](const QString& strText, int nY, int nSize,
                                  const QColor& oInk, bool bBold)
            {
                auto oFont = font();
                oFont.setPixelSize(nSize);
                oFont.setBold(bBold);
                oPainter.setFont(oFont);
                oPainter.setPen(isEnabled() ? oInk : oColors.m_oDisabled);
                oPainter.drawText(QRect(nTextLeft, nY, nTextWidth, 22),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  oPainter.fontMetrics().elidedText(strText, Qt::ElideRight,
                                                                     nTextWidth));
            };
            fnDrawText("继续阅读", 9, 12, oColors.m_oAccent, true);
            fnDrawText(m_oEntry.m_strTitle, 31, 16, oColors.m_oText, true);
            const int nProgress = qBound(0, qRound(m_oEntry.m_fProgress * 100), 100);
            const auto strPosition = m_oEntry.m_strChapterTitle.isEmpty()
                ? QString("%1%").arg(nProgress)
                : QString("%1%  ·  %2").arg(nProgress).arg(m_oEntry.m_strChapterTitle);
            fnDrawText(strPosition, 55, 12, oColors.m_oMuted, false);

            oPainter.setPen(Qt::NoPen);
            oPainter.setBrush(oColors.m_oSection);
            const QRect oProgress(nTextLeft, 87, nTextWidth, 4);
            oPainter.drawRoundedRect(oProgress, 2, 2);
            if (nProgress > 0)
            {
                oPainter.setBrush(oColors.m_oAccent);
                oPainter.drawRoundedRect(QRect(oProgress.topLeft(),
                    QSize(qMax(4, oProgress.width() * nProgress / 100), oProgress.height())), 2, 2);
            }
            makeIcon(Icon::Right, oColors.m_oAccent, oColors.m_oDisabled, oColors.m_oAccent)
                .paint(&oPainter, QRect(width() - 31, 44, 18, 18), Qt::AlignCenter,
                       isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }

      private:
        ReadingEntry m_oEntry;
        QImage m_oCover;
        Theme m_eTheme = Theme::Light;
    };

    void ClientWindow::buildMobileLibrary(QVBoxLayout* pLibrary, QHBoxLayout* pBar)
    {
        m_bMobileShelfList = m_oSettings.value("mobileShelfList", false).toBool();
        m_pMobileContinue = new MobileContinueCard;
        m_pMobileContinue->hide();
        pLibrary->addWidget(m_pMobileContinue);
        connect(m_pMobileContinue, &QPushButton::clicked, this, [this]
        {
            if (m_pMobileContinue->book() > 0)
            {
                openBook(m_pMobileContinue->book(), true);
            }
        });

        m_pMobileShelfView = new QWidget;
        m_pMobileShelfView->setObjectName("mobileShelfView");
        m_pMobileShelfView->setAttribute(Qt::WA_StyledBackground);
        auto* pControls = new QHBoxLayout(m_pMobileShelfView);
        pControls->setContentsMargins(2, 2, 2, 2);
        pControls->setSpacing(2);
        auto* pChoices = new QButtonGroup(m_pMobileShelfView);
        pChoices->setExclusive(true);
        m_pMobileGrid = new QPushButton;
        m_pMobileGrid->setObjectName("mobileShelfGrid");
        m_pMobileList = new QPushButton;
        m_pMobileList->setObjectName("mobileShelfList");
        for (auto* pChoice : {m_pMobileGrid, m_pMobileList})
        {
            const bool bList = pChoice == m_pMobileList;
            pChoice->setAccessibleName(bList ? "紧凑列表" : "封面网格");
            pChoice->setToolTip(pChoice->accessibleName());
            pChoice->setCheckable(true);
            pChoice->setChecked(bList == m_bMobileShelfList);
            pChoice->setFixedWidth(48);
            setButtonIcon(pChoice, bList ? Icon::List : Icon::Library);
            pChoices->addButton(pChoice);
            pControls->addWidget(pChoice);
            connect(pChoice, &QPushButton::clicked, this, [this, bList]
            {
                if (m_bMobileShelfList != bList)
                {
                    m_bMobileShelfList = bList;
                    m_oSettings.setValue("mobileShelfList", bList);
                    updateMobileLibraryLayout();
                }
            });
        }
        pBar->addWidget(m_pMobileShelfView);
        m_pMobileShelfView->hide();
    }

    void ClientWindow::updateMobileLibraryLayout()
    {
        if (!m_pCards)
        {
            return;
        }
        const bool bTouch = mobileMode();
        const bool bLibrary = m_bUnlocked && m_pPages->currentIndex() == 1;
        const bool bShelf = m_nSection == 0 && m_strActiveSearch.isEmpty();
        const bool bCompactCards = bTouch && (!bShelf || m_bMobileShelfList);
        m_pMobileShelfView->setVisible(bTouch && bLibrary && bShelf);
        m_pMobileContinue->setVisible(bTouch && bLibrary && bShelf && height() >= 500 &&
                                      m_pMobileContinue->book() > 0);
        if (m_pCards->property("mobileCards").toBool() != bTouch ||
            m_pCards->property("mobileCompactCards").toBool() != bCompactCards)
        {
            m_pCards->setProperty("mobileCards", bTouch);
            m_pCards->setProperty("mobileCompactCards", bCompactCards);
            m_pCards->doItemsLayout();
        }
        const bool bPaged = m_nSection == 1 && m_strActiveSearch.isEmpty();
        m_pPreviousPage->setVisible(bPaged);
        m_pNextPage->setVisible(bPaged);
    }

    void ClientWindow::applyMobileLibraryStyle()
    {
        if (!mobileMode())
        {
            return;
        }
        const auto oColors = colors(m_eTheme);
        m_pMobileTitle->setStyleSheet(QString("QLabel {font-size:21px; font-weight:600; color:%1}")
                                         .arg(oColors.m_oText.name()));
        m_pMobileNavigation->setStyleSheet(
            QString("QWidget#mobileNavigation {background:%1; border:1px solid %2; border-radius:16px}")
                .arg(oColors.m_oPanel.name(), oColors.m_oBorder.name()));
        m_pMobileShelfView->setStyleSheet(
            QString("QWidget#mobileShelfView {background:%1; border-radius:13px}")
                .arg(oColors.m_oSection.name()));
        for (auto* pChoice : {m_pMobileGrid, m_pMobileList})
        {
            pChoice->setStyleSheet(QString("QPushButton {background:transparent; border:0; "
                                          "border-radius:11px; padding:0} "
                                          "QPushButton:checked {background:%1} "
                                          "QPushButton:pressed {background:%2}")
                                        .arg(oColors.m_oSelection.name(), oColors.m_oHover.name()));
        }
        m_pSearch->setStyleSheet(
            QString("QLineEdit {font-size:13px; background:%1; color:%2; "
                    "border:1px solid %3; border-radius:16px; padding:0 10px} "
                    "QLineEdit:focus {border-color:%4}")
                .arg(oColors.m_oSection.name(), oColors.m_oText.name(),
                     oColors.m_oBorder.name(), oColors.m_oAccent.name()));
        m_pMobileContinue->setTheme(m_eTheme);
    }

    void ClientWindow::updateMobileContinueReading()
    {
        if (!m_bUnlocked || !mobileMode() || m_pPages->currentIndex() != 1 || m_nSection != 0 ||
            !m_strActiveSearch.isEmpty())
        {
            return;
        }
        const auto vHistory = m_pReadingState->history();
        if (vHistory.isEmpty())
        {
            m_pMobileContinue->setBook({}, {});
        }
        else
        {
            const auto& oLatest = vHistory.front();
            const bool bChangedBook = m_pMobileContinue->book() != oLatest.m_nNovel;
            const auto oCover = m_pCovers->image(oLatest.m_nNovel);
            m_pMobileContinue->setBook(oLatest, oCover);
            if (bChangedBook && oCover.isNull() && m_pStore)
            {
                requestCover(oLatest.m_nNovel);
            }
        }
        updateMobileLibraryLayout();
    }

    void ClientWindow::updateMobileContinueCover(int nNovel, const QImage& oCover)
    {
        if (m_pMobileContinue && m_pMobileContinue->book() == nNovel)
        {
            m_pMobileContinue->setCover(oCover);
        }
    }
} // namespace Wenku8::Reader
