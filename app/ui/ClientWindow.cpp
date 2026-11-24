// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "DownloadManager.h"
#include "DownloadTaskPanel.h"
#include "ReadAloudController.h"
#include "Toast.h"
#include "AppInfo.h"
#include "DownloadOption.h"
#include "ExportDestination.h"
#include <QDesktopServices>
#include "PullRefreshIndicator.h"
#include <QMenuBar>
#include <QMenu>
#include "Icons.h"
#include "ThemeSelector.h"
#include "FlowLayout.h"
#include "AnimatedStack.h"
#include "LoadingPanel.h"
#include "TouchScroll.h"
#include "AndroidKeys.h"
#include <QInputMethod>
#include <QButtonGroup>
#ifdef WENKU8_CLIENT_ONLINE
#include "OnlineSession.h"
#endif
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QFrame>
#include <QStackedWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QProgressBar>
#include <QCheckBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QScopedValueRollback>
#include <QCloseEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QTextCursor>
#include <QScrollBar>
#include <QSlider>
#include <QSignalBlocker>
#include <algorithm>
#include <functional>
#include <QShortcut>
#include <QDialog>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QTextBlock>
#include <QTextLayout>
#include <QHeaderView>
#include <QLinearGradient>
#include <QItemSelectionModel>
#include <QScroller>
#include <QKeyEvent>
#include <QScreen>
#include <QtMath>
#include <QWindow>
#include <QFile>
namespace Wenku8::Reader
{
    namespace
    {
        QString text(const std::string& strS)
        {
            return QString::fromUtf8(strS.data(), int(strS.size()));
        }
        QLabel* label(const QString& strValue, const char* pName)
        {
            auto pL = new QLabel(strValue);
            pL->setObjectName(pName);
            pL->setTextFormat(Qt::PlainText);
            return pL;
        }
        class ElidedLabel final : public QLabel
        {
          public:
            explicit ElidedLabel(int nLines = 1)
                : m_nLines(nLines)
            {
                setTextFormat(Qt::PlainText);
                setWordWrap(nLines > 1);
            }

            QSize sizeHint() const override
            {
                auto oSize = QLabel::sizeHint();
                oSize.setHeight(qMin(oSize.height(), maximumTextHeight()));
                return oSize;
            }

            int heightForWidth(int nWidth) const override
            {
                return qMin(QLabel::heightForWidth(nWidth), maximumTextHeight());
            }

          protected:
            void paintEvent(QPaintEvent*) override
            {
                QPainter oPainter(this);
                oPainter.setFont(font());
                oPainter.setPen(palette().color(foregroundRole()));
                const auto oArea = contentsRect();
                const auto oMetrics = fontMetrics();
                if (m_nLines == 1)
                {
                    oPainter.drawText(oArea, alignment() | Qt::AlignVCenter,
                                      oMetrics.elidedText(text(), Qt::ElideRight, oArea.width()));
                    return;
                }
                QTextLayout oLayout(text(), font());
                QTextOption oOption;
                oOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                oLayout.setTextOption(oOption);
                const int nLines = qMin(m_nLines, 1 + qMax(0, oArea.height() - oMetrics.height()) /
                                                          oMetrics.lineSpacing());
                oLayout.beginLayout();
                for (int nLine = 0; nLine < nLines; ++nLine)
                {
                    auto oLine = oLayout.createLine();
                    if (!oLine.isValid())
                    {
                        break;
                    }
                    oLine.setLineWidth(oArea.width());
                    const auto oPosition = oArea.topLeft() + QPoint(0, nLine * oMetrics.lineSpacing());
                    if (nLine + 1 < nLines)
                    {
                        oLine.draw(&oPainter, oPosition);
                    }
                    else
                    {
                        oPainter.drawText(oPosition + QPoint(0, oMetrics.ascent()),
                            oMetrics.elidedText(text().mid(oLine.textStart()), Qt::ElideRight,
                                                oArea.width()));
                    }
                }
                oLayout.endLayout();
            }

          private:
            int maximumTextHeight() const
            {
                const auto oMetrics = fontMetrics();
                return oMetrics.height() + (m_nLines - 1) * oMetrics.lineSpacing();
            }

            int m_nLines;
        };
        class ActionButton final : public QPushButton
        {
          protected:
            void changeEvent(QEvent* pEvent) override
            {
                QPushButton::changeEvent(pEvent);
                if (pEvent->type() == QEvent::EnabledChange)
                    setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
            }

          public:
            using QPushButton::QPushButton;
        };
        QPushButton* button(const QString& strValue, const char* pName = "")
        {
            auto pB = new ActionButton(strValue);
            pB->setObjectName(pName);
            pB->setCursor(Qt::PointingHandCursor);
            return pB;
        }
        class NavigationButton final : public QPushButton
        {
          public:
            using QPushButton::QPushButton;
            QSize sizeHint() const override
            {
                return {68, 56};
            }

          protected:
            void paintEvent(QPaintEvent*) override
            {
                QPainter oPainter(this);
                oPainter.setRenderHint(QPainter::Antialiasing);
                auto oInk = property(isChecked() ? "navActive" : "navMuted").value<QColor>();
                if (!oInk.isValid())
                    oInk = palette().color(QPalette::ButtonText);
                if (isDown() || isChecked())
                {
                    oPainter.setPen(Qt::NoPen);
                    oPainter.setBrush(property(isDown() ? "navPressed" : "navSelection").value<QColor>());
                    oPainter.drawRoundedRect(rect().adjusted(2, 1, -2, -1), 14, 14);
                }
                makeIcon(Icon(property("wenkuIcon").toInt()), oInk,
                         property("navDisabled").value<QColor>(), oInk)
                    .paint(&oPainter, QRect((width() - 20) / 2, 7, 20, 20), Qt::AlignCenter,
                           isEnabled() ? QIcon::Normal : QIcon::Disabled);
                auto oF = font();
                oF.setPixelSize(12);
                oF.setBold(isChecked());
                oPainter.setFont(oF);
                oPainter.setPen(isEnabled() ? oInk : property("navDisabled").value<QColor>());
                oPainter.drawText(QRect(0, 28, width(), 23), Qt::AlignCenter, text());
            }
        };
        class SearchInput final : public QLineEdit
        {
            QString m_strHint;

          protected:
            void paintEvent(QPaintEvent* pEvent) override
            {
                QLineEdit::paintEvent(pEvent);
                QPainter oIconPainter(this);
                const auto oInk = property("hintColor").value<QColor>();
                makeIcon(Icon::Search, oInk, oInk, oInk)
                    .paint(&oIconPainter, QRect(12, (height() - 18) / 2, 18, 18));
                oIconPainter.end();
                if (text().isEmpty())
                {
                    QPainter oPainter(this);
                    QColor oHint = property("hintColor").value<QColor>();
                    oPainter.setPen(oHint.isValid() ? oHint : QColor("#777c76"));
                    auto oArea = contentsRect().adjusted(38, 0, -26, 0);
                    oPainter.drawText(
                        oArea, Qt::AlignVCenter | Qt::AlignLeft,
                        fontMetrics().elidedText(m_strHint, Qt::ElideRight, oArea.width()));
                }
            }

          public:
            explicit SearchInput(QString strHint) : m_strHint(std::move(strHint))
            {
                setAccessibleName(m_strHint);
                setTextMargins(26, 0, 0, 0);
            }
        };
        class ChapterRows final : public QStyledItemDelegate
        {
          public:
            Theme m_eTheme = Theme::Light;
            using QStyledItemDelegate::QStyledItemDelegate;
            QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& oIndex) const override
            {
                return {180, oIndex.parent().isValid() ? 48 : 54};
            }
            void paint(QPainter* pP, const QStyleOptionViewItem& oO,
                       const QModelIndex& oIndex) const override
            {
                auto oC = colors(m_eTheme);
                bool bChapter = oIndex.sibling(oIndex.row(), 0).data(Qt::UserRole).isValid();
                bool bEnabled = oO.state & QStyle::State_Enabled,
                     bSelected = oO.state & QStyle::State_Selected,
                     bHover = bEnabled && (oO.state & QStyle::State_MouseOver);
                QRect oR = oO.rect.adjusted(0, 2, 0, -2);
                pP->save();
                pP->setRenderHint(QPainter::Antialiasing);
                QColor oBg = !bChapter   ? oC.m_oSection
                             : bSelected ? oC.m_oSelection
                             : bHover    ? oC.m_oHover
                                         : oC.m_oPanel;
                QLinearGradient oGradient(oR.topLeft(), oR.topRight());
                oGradient.setColorAt(0, oBg);
                oGradient.setColorAt(1, (bHover && !bSelected) ? oC.m_oPanel : oBg);
                pP->fillRect(oR, oGradient);
                pP->setPen(QPen(oC.m_oBorder, 1));
                pP->drawLine(oR.bottomLeft(), oR.bottomRight());
                if (bChapter && (bHover || bSelected) && oIndex.column() == 0)
                    pP->fillRect(QRect(oR.left(), oR.top(), 3, oR.height()), oC.m_oSelectionText);
                auto oInk = !bEnabled              ? oC.m_oDisabled
                            : bSelected            ? oC.m_oSelectionText
                            : !bChapter            ? oC.m_oText
                            : oIndex.column() == 1 ? oC.m_oAccent
                                                   : oC.m_oText;
                pP->setPen(oInk);
                auto oFont = oO.font;
                oFont.setBold(!bChapter || bSelected);
                oFont.setPointSize(oIndex.column() == 0 ? 10 : 9);
                pP->setFont(oFont);
                QRect oLabel = oR.adjusted(12, 0, -12, 0);
                if (oIndex.column() == 0)
                {
                    makeIcon(bChapter ? Icon::Book : Icon::List, oInk, oC.m_oDisabled,
                             oC.m_oSelectionText)
                        .paint(pP, QRect(oLabel.left(), oR.center().y() - 8, 16, 16));
                    oLabel.adjust(26, 0, 0, 0);
                }
                pP->drawText(oLabel, Qt::AlignVCenter | Qt::AlignLeft,
                             pP->fontMetrics().elidedText(oIndex.data().toString(), Qt::ElideRight,
                                                          oLabel.width()));
                pP->restore();
            }
        };
        bool hasLazyCovers(const QListWidget* pCards)
        {
            return pCards->count() > 0 && pCards->item(0)->data(nLazyCoverRole).toBool();
        }
        class BookCards final : public QStyledItemDelegate
        {
          public:
            Theme m_eTheme = Theme::Light;
            BookCards(QListWidget* pView, CoverStore* pCovers, std::function<void(int)> fnRequestCover)
                : QStyledItemDelegate(pView), m_pView(pView), m_pCovers(pCovers),
                  m_fnRequestCover(std::move(fnRequestCover))
            {
            }
            QSize sizeHint(const QStyleOptionViewItem& oO, const QModelIndex&) const override
            {
                auto pView = qobject_cast<const QListWidget*>(parent());
                // Reserve the scrollbar gutter in both layouts. Basing mobile cells
                // on the live viewport creates a feedback loop at wrapping/height
                // boundaries (for example three search results in two columns).
                const int nWidth = pView ? qMax(1, pView->maximumViewportSize().width() -
                                                     pView->verticalScrollBar()->sizeHint().width())
                                        : oO.rect.width();
                if (pView && pView->property("mobileCards").toBool())
                {
                    if (pView->property("mobileCompactCards").toBool())
                    {
                        return {qMax(1, nWidth - 14), 124};
                    }
                    const int nColumns = qBound(2, nWidth / 116, 5);
                    const int nCell = qMax(100, (nWidth - nColumns * 12 - 2) / nColumns);
                    return {nCell, qRound(qMin(136, nCell - 26) * 1.42) + 104};
                }
                if (pView && pView->property("desktopCompactCards").toBool())
                {
                    const int nRowWidth = qMax(1, nWidth - 14);
                    return {nRowWidth, nRowWidth >= nWideDesktopRow + 3 ? 72 : 84};
                }
                int nColumns = qMax(1, (nWidth + 6) / 322);
                return {qMax(240, (nWidth - 12 - (nColumns - 1) * 12) / nColumns), 188};
            }
            void paint(QPainter* pP, const QStyleOptionViewItem& oO,
                       const QModelIndex& oIndex) const override
            {
                auto* pView = qobject_cast<const QListWidget*>(parent());
                if (pView && pView->property("mobileCards").toBool())
                {
                    paintMobile(pP, oO, oIndex, pView->property("mobileCompactCards").toBool());
                    return;
                }
                if (pView && pView->property("desktopCompactCards").toBool())
                {
                    paintDesktopList(pP, oO, oIndex);
                    return;
                }
                auto oC = colors(m_eTheme);
                pP->save();
                pP->setRenderHint(QPainter::Antialiasing);
                QRect oR = oO.rect.adjusted(4, 4, -8, -8);
                bool bEnabled = oO.state & QStyle::State_Enabled,
                     bSelected = oO.state & QStyle::State_Selected,
                     bHover = bEnabled && (oO.state & QStyle::State_MouseOver);
                QLinearGradient oGradient(oR.topLeft(), oR.bottomRight());
                oGradient.setColorAt(0, bSelected ? oC.m_oSelection
                                        : bHover  ? oC.m_oHover
                                                  : oC.m_oPanel);
                oGradient.setColorAt(1, oC.m_oPanel);
                pP->setBrush(oGradient);
                pP->setPen(QPen(bSelected || bHover ? oC.m_oSelectionText : oC.m_oBorder,
                                bSelected ? 2 : 1));
                pP->drawRoundedRect(oR, 14, 14);
                QRect oCover(oR.left() + 18, oR.top() + 19, 74, 118);
                const auto oImage = coverImage(oIndex);
                QPainterPath oClip;
                oClip.addRoundedRect(oCover, 6, 6);
                pP->save();
                pP->setClipPath(oClip);
                if (!oImage.isNull())
                {
                    pP->setRenderHint(QPainter::SmoothPixmapTransform);
                    QSizeF oSource = oImage.size();
                    oSource.scale(QSizeF(oCover.size()), Qt::KeepAspectRatioByExpanding);
                    QRectF oTarget(QPointF(oCover.center()) -
                                       QPointF(oSource.width() / 2, oSource.height() / 2),
                                   oSource);
                    pP->drawImage(oTarget, oImage);
                }
                else
                {
                    pP->fillRect(oCover, oC.m_oSection);
                    makeIcon(Icon::Book, oC.m_oMuted, oC.m_oDisabled, oC.m_oAccent)
                        .paint(pP,
                               QRect(oCover.center().x() - 13, oCover.center().y() - 24, 26, 26));
                    auto oF = oO.font;
                    oF.setPointSize(7);
                    pP->setFont(oF);
                    pP->setPen(oC.m_oMuted);
                    const auto strPlaceholder = coverPlaceholder(oIndex);
                    pP->drawText(oCover.adjusted(2, 62, -2, -4), Qt::AlignCenter | Qt::TextWordWrap,
                                 strPlaceholder.isEmpty() ? "封面加载中" : strPlaceholder);
                }
                pP->restore();
                int nX = oCover.right() + 16, nW = oR.right() - nX - 14;
                auto oFont = oO.font;
                oFont.setPointSize(11);
                oFont.setBold(true);
                pP->setFont(oFont);
                pP->setPen(bEnabled ? oC.m_oText : oC.m_oDisabled);
                const QRect oTitle(nX, oR.top() + 20, nW, 52);
                pP->save();
                pP->setClipRect(oTitle);
                pP->drawText(oTitle, Qt::TextWrapAnywhere, oIndex.data().toString());
                pP->restore();
                paintMetadata(pP, oO, QRect(nX, oR.top() + 77, nW, 20),
                              oIndex.data(Qt::UserRole + 1).toString(), {});
                paintMetadata(pP, oO, QRect(nX, oR.top() + 103, nW, 22), {},
                              oIndex.data(Qt::UserRole + 2).toString());
                oFont.setPointSize(9);
                oFont.setBold(false);
                pP->setFont(oFont);
                pP->setPen(oC.m_oMuted);
                const QRect oPosition(oR.left() + 18, oR.top() + 144, oR.width() - 53, 22);
                const auto strProgress = oIndex.data(Qt::UserRole + 4).toString();
                pP->drawText(oPosition, Qt::AlignVCenter,
                             pP->fontMetrics().elidedText(strProgress.isEmpty()
                                 ? oIndex.data(Qt::UserRole + 5).toString() : strProgress,
                                 Qt::ElideRight, oPosition.width()));
                if (bHover || bSelected)
                    makeIcon(Icon::Right, oC.m_oSelectionText, oC.m_oDisabled, oC.m_oSelectionText)
                        .paint(pP, QRect(oR.right() - 28, oR.bottom() - 29, 16, 16));
                pP->restore();
            }

          private:
            static constexpr int nWideDesktopRow = 720;
            const QListWidget* m_pView;
            CoverStore* m_pCovers;
            std::function<void(int)> m_fnRequestCover;
            mutable QSet<int> m_vQueuedCovers;

            void paintMetadata(QPainter* pPainter, const QStyleOptionViewItem& oOption,
                               const QRect& oArea, const QString& strAuthor,
                               const QString& strStatus) const
            {
                if (oArea.isEmpty() || (strAuthor.isEmpty() && strStatus.isEmpty()))
                {
                    return;
                }
                const auto oColors = colors(m_eTheme);
                const bool bEnabled = oOption.state & QStyle::State_Enabled;
                const bool bSelected = oOption.state & QStyle::State_Selected;
                auto oFont = oOption.font;
                oFont.setPixelSize(12);
                oFont.setBold(false);
                pPainter->save();
                pPainter->setFont(oFont);
                const auto oMetrics = pPainter->fontMetrics();
                QRect oAuthor = oArea;
                if (!strStatus.isEmpty())
                {
                    const int nLimit = strAuthor.isEmpty() ? oArea.width() : oArea.width() / 2;
                    const int nBadgeWidth = qMin(nLimit, oMetrics.horizontalAdvance(strStatus) + 14);
                    const int nBadgeLeft = strAuthor.isEmpty() ? oArea.left()
                                                               : oArea.right() - nBadgeWidth + 1;
                    const QRect oBadge(nBadgeLeft, oArea.top() + 1, nBadgeWidth, oArea.height() - 2);
                    pPainter->setPen(Qt::NoPen);
                    pPainter->setBrush(bSelected ? oColors.m_oSection : oColors.m_oSelection);
                    pPainter->drawRoundedRect(oBadge, 6, 6);
                    pPainter->setPen(bEnabled ? (bSelected ? oColors.m_oText : oColors.m_oSelectionText)
                                              : oColors.m_oDisabled);
                    pPainter->drawText(oBadge.adjusted(7, 0, -7, 0), Qt::AlignCenter,
                                       oMetrics.elidedText(strStatus, Qt::ElideRight,
                                                           qMax(0, nBadgeWidth - 14)));
                    oAuthor.setRight(nBadgeLeft - 8);
                }
                if (!strAuthor.isEmpty() && oAuthor.width() > 0)
                {
                    pPainter->setPen(bEnabled ? oColors.m_oMuted : oColors.m_oDisabled);
                    pPainter->drawText(oAuthor, Qt::AlignLeft | Qt::AlignVCenter,
                                       oMetrics.elidedText(strAuthor, Qt::ElideRight, oAuthor.width()));
                }
                pPainter->restore();
            }

            QImage coverImage(const QModelIndex& oIndex) const
            {
                if (!oIndex.data(nLazyCoverRole).toBool())
                {
                    return oIndex.data(Qt::UserRole + 3).value<QImage>();
                }
                const int nId = oIndex.data(Qt::UserRole).toInt();
                const auto oImage = m_pCovers->thumbnail(nId);
                if (oImage.isNull() && !m_vQueuedCovers.contains(nId) &&
                    m_pCovers->canRequestThumbnail(nId))
                {
                    m_vQueuedCovers.insert(nId);
                    QTimer::singleShot(0, this, [this, nId, oPending = QPersistentModelIndex(oIndex)]
                    {
                        m_vQueuedCovers.remove(nId);
                        if (oPending.isValid() && m_pView->isVisible() &&
                            m_pView->visualRect(oPending).intersects(m_pView->viewport()->rect()) &&
                            m_pCovers->canRequestThumbnail(nId))
                        {
                            m_fnRequestCover(nId);
                        }
                    });
                }
                return oImage;
            }

            QString coverPlaceholder(const QModelIndex& oIndex) const
            {
                return oIndex.data(nLazyCoverRole).toBool()
                           ? m_pCovers->placeholder(oIndex.data(Qt::UserRole).toInt())
                           : oIndex.data(Qt::UserRole + 6).toString();
            }

            void paintDesktopList(QPainter* pPainter, const QStyleOptionViewItem& oOption,
                                  const QModelIndex& oIndex) const
            {
                const auto oColors = colors(m_eTheme);
                const QRect oRow = oOption.rect.adjusted(1, 1, -2, -2);
                const bool bEnabled = oOption.state & QStyle::State_Enabled;
                const bool bSelected = oOption.state & QStyle::State_Selected;
                const bool bHover = bEnabled && (oOption.state & QStyle::State_MouseOver);
                const bool bWide = oRow.width() >= nWideDesktopRow;
                pPainter->save();
                pPainter->setRenderHint(QPainter::Antialiasing);
                pPainter->setBrush(bSelected ? oColors.m_oSelection
                                             : bHover ? oColors.m_oHover : oColors.m_oPanel);
                pPainter->setPen(QPen(bSelected ? oColors.m_oSelectionText : oColors.m_oBorder, 1));
                pPainter->drawRoundedRect(oRow, 10, 10);

                const QRect oCover(oRow.left() + 9, oRow.top() + 7, bWide ? 36 : 43,
                                   oRow.height() - 14);
                QPainterPath oCoverClip;
                oCoverClip.addRoundedRect(oCover, 6, 6);
                pPainter->save();
                pPainter->setClipPath(oCoverClip);
                const auto oImage = coverImage(oIndex);
                if (oImage.isNull())
                {
                    pPainter->fillRect(oCover, oColors.m_oSection);
                    makeIcon(Icon::Book, oColors.m_oMuted, oColors.m_oDisabled, oColors.m_oAccent)
                        .paint(pPainter, QRect(oCover.center() - QPoint(10, 10), QSize(20, 20)));
                }
                else
                {
                    auto oSize = oImage.size();
                    oSize.scale(oCover.size(), Qt::KeepAspectRatioByExpanding);
                    QRect oTarget(QPoint{}, oSize);
                    oTarget.moveCenter(oCover.center());
                    pPainter->setRenderHint(QPainter::SmoothPixmapTransform);
                    pPainter->drawImage(oTarget, oImage);
                }
                pPainter->restore();

                const int nLeft = oCover.right() + 13;
                const int nWidth = oRow.right() - nLeft - 42;
                auto fnDrawText = [&](const QString& strText, const QRect& oBounds, int nSize,
                                      const QColor& oInk, bool bBold)
                {
                    auto oFont = oOption.font;
                    oFont.setPixelSize(nSize);
                    oFont.setBold(bBold);
                    pPainter->setFont(oFont);
                    pPainter->setPen(bEnabled ? oInk : oColors.m_oDisabled);
                    pPainter->drawText(oBounds, Qt::AlignVCenter | Qt::AlignLeft,
                        pPainter->fontMetrics().elidedText(strText, Qt::ElideRight,
                                                          oBounds.width()));
                };
                const auto strAuthor = oIndex.data(Qt::UserRole + 1).toString();
                const auto strStatus = oIndex.data(Qt::UserRole + 2).toString();
                const auto strReading = oIndex.data(Qt::UserRole + 4).toString();
                const auto strChapter = strReading.isEmpty()
                    ? oIndex.data(Qt::UserRole + 5).toString() : strReading;
                const int nTitleWidth = bWide ? nWidth * 53 / 100 - 16 : nWidth;
                const int nTitleY = bWide ? 13 : strChapter.isEmpty() ? 18 : 7;
                const int nDetailLeft = bWide ? nLeft + nTitleWidth + 16 : nLeft;
                const int nDetailWidth = nLeft + nWidth - nDetailLeft;
                const int nTop = oRow.top();
                fnDrawText(oIndex.data().toString(), QRect(nLeft, nTop + nTitleY, nTitleWidth, 20),
                           15, oColors.m_oText, true);
                paintMetadata(pPainter, oOption,
                              QRect(nLeft, nTop + nTitleY + 22, nTitleWidth, 20), strAuthor,
                              bWide ? QString() : strStatus);
                if (bWide)
                {
                    paintMetadata(pPainter, oOption,
                                  QRect(nDetailLeft, nTop + (strChapter.isEmpty() ? 24 : 11),
                                        nDetailWidth, 20), {}, strStatus);
                }
                const int nChapterY = bWide ? (strStatus.isEmpty() ? 24 : 33) : 48;
                fnDrawText(strChapter, QRect(nDetailLeft, nTop + nChapterY, nDetailWidth, 20),
                           12, strReading.isEmpty() ? oColors.m_oMuted : oColors.m_oAccent, false);
                if (const auto oProgress = oIndex.data(Qt::UserRole + 7); oProgress.isValid())
                {
                    const QRect oTrack(nDetailLeft, oRow.bottom() - 6, qMin(nDetailWidth, 240), 3);
                    const int nValue = qBound(0, oProgress.toInt(), 100);
                    pPainter->setPen(Qt::NoPen);
                    pPainter->setBrush(oColors.m_oBorder);
                    pPainter->drawRoundedRect(oTrack, 1, 1);
                    if (nValue > 0)
                    {
                        pPainter->setBrush(oColors.m_oAccent);
                        pPainter->drawRoundedRect(QRect(oTrack.topLeft(),
                            QSize(qMax(3, oTrack.width() * nValue / 100), 3)), 1, 1);
                    }
                }
                const auto oChevron = bSelected || bHover ? oColors.m_oSelectionText : oColors.m_oMuted;
                makeIcon(Icon::Right, oChevron, oColors.m_oDisabled, oChevron)
                    .paint(pPainter, QRect(oRow.right() - 26, oRow.center().y() - 8, 16, 16));
                pPainter->restore();
            }

            void paintMobile(QPainter* pPainter, const QStyleOptionViewItem& oOption,
                             const QModelIndex& oIndex, bool bList) const
            {
                const auto oColors = colors(m_eTheme);
                const auto oCard = oOption.rect.adjusted(0, 0, -1, -1);
                const bool bSelected = oOption.state & QStyle::State_Selected;
                const bool bEnabled = oOption.state & QStyle::State_Enabled;
                pPainter->save();
                pPainter->setRenderHint(QPainter::Antialiasing);
                pPainter->setBrush(bSelected ? oColors.m_oSelection : oColors.m_oPanel);
                pPainter->setPen(QPen(bSelected ? oColors.m_oSelectionText : oColors.m_oBorder, 1));
                pPainter->drawRoundedRect(oCard, 14, 14);
                const int nCoverWidth = bList ? 62 : qMin(136, oCard.width() - 26);
                const int nCoverHeight = qRound(nCoverWidth * 1.42);
                const QRect oCover(bList ? oCard.left() + 12 : oCard.center().x() - nCoverWidth / 2,
                    bList ? oCard.center().y() - nCoverHeight / 2 : oCard.top() + 10,
                    nCoverWidth, nCoverHeight);
                QPainterPath oClip;
                oClip.addRoundedRect(oCover, 8, 8);
                pPainter->save();
                pPainter->setClipPath(oClip);
                pPainter->fillRect(oCover, oColors.m_oSection);
                const auto oImage = coverImage(oIndex);
                if (oImage.isNull())
                {
                    makeIcon(Icon::Book, oColors.m_oMuted, oColors.m_oDisabled, oColors.m_oAccent)
                        .paint(pPainter, QRect(oCover.center() - QPoint(13, 18), QSize(26, 26)));
                    auto oFont = oOption.font;
                    oFont.setPixelSize(10);
                    pPainter->setFont(oFont);
                    pPainter->setPen(oColors.m_oMuted);
                    pPainter->drawText(QRect(oCover.left() + 3, oCover.center().y() + 12,
                                             oCover.width() - 6, 21),
                                       Qt::AlignCenter,
                                       pPainter->fontMetrics().elidedText(
                                           coverPlaceholder(oIndex), Qt::ElideRight,
                                           oCover.width() - 6));
                }
                else
                {
                    auto oScaled = oImage.size();
                    oScaled.scale(oCover.size(), Qt::KeepAspectRatioByExpanding);
                    QRect oTarget(QPoint{}, oScaled);
                    oTarget.moveCenter(oCover.center());
                    pPainter->setRenderHint(QPainter::SmoothPixmapTransform);
                    pPainter->drawImage(oTarget, oImage);
                }
                pPainter->restore();

                const int nTextLeft = bList ? oCover.right() + 13 : oCard.left() + 10;
                const int nTextRight = oCard.right() - (bList ? 31 : 9);
                const int nTextWidth = nTextRight - nTextLeft;
                auto fnText = [&](const QString& strText, int nY, int nSize, const QColor& oInk,
                                  bool bBold)
                {
                    auto oFont = oOption.font;
                    oFont.setPixelSize(nSize);
                    oFont.setBold(bBold);
                    pPainter->setFont(oFont);
                    pPainter->setPen(bEnabled ? oInk : oColors.m_oDisabled);
                    const auto strLine =
                        pPainter->fontMetrics().elidedText(strText, Qt::ElideRight, nTextWidth);
                    pPainter->drawText(QRect(nTextLeft, nY, nTextWidth, 20),
                                       Qt::AlignLeft | Qt::AlignVCenter, strLine);
                };
                const auto strAuthor = oIndex.data(Qt::UserRole + 1).toString();
                const auto strStatus = oIndex.data(Qt::UserRole + 2).toString();
                const auto oReadingProgress = oIndex.data(Qt::UserRole + 7);
                if (bList)
                {
                    fnText(oIndex.data().toString(), oCard.top() + 16, 15, oColors.m_oText, true);
                    paintMetadata(pPainter, oOption,
                                  QRect(nTextLeft, oCard.top() + 43, nTextWidth, 22),
                                  strAuthor, strStatus);
                    const auto strChapter = oIndex.data(Qt::UserRole + 5).toString();
                    const auto strPosition = oIndex.data(Qt::UserRole + 4).toString();
                    fnText(strPosition.isEmpty() ? strChapter : strPosition,
                           oCard.top() + 69, 12, oColors.m_oAccent, false);
                    makeIcon(Icon::Right, oColors.m_oMuted, oColors.m_oDisabled, oColors.m_oAccent)
                        .paint(pPainter,
                               QRect(oCard.right() - 24, oCard.center().y() - 8, 16, 16));
                }
                else
                {
                    auto oTitleFont = oOption.font;
                    oTitleFont.setBold(true);
                    oTitleFont.setPixelSize(13);
                    pPainter->setFont(oTitleFont);
                    pPainter->setPen(bEnabled ? oColors.m_oText : oColors.m_oDisabled);
                    const QRect oTitleRect(nTextLeft, oCover.bottom() + 7, nTextWidth, 36);
                    pPainter->save();
                    pPainter->setClipRect(oTitleRect);
                    pPainter->drawText(oTitleRect, Qt::AlignLeft | Qt::TextWrapAnywhere,
                                       oIndex.data().toString());
                    pPainter->restore();
                    paintMetadata(pPainter, oOption,
                                  QRect(nTextLeft, oCover.bottom() + 42, nTextWidth, 19),
                                  strAuthor, {});
                    paintMetadata(pPainter, oOption,
                                  QRect(nTextLeft, oCover.bottom() + 62, nTextWidth, 20),
                                  {}, strStatus);
                }
                if (oReadingProgress.isValid())
                {
                    const int nValue = qBound(0, oReadingProgress.toInt(), 100);
                    const QRect oTrack(nTextLeft, oCard.bottom() - 11, nTextWidth, 3);
                    pPainter->setPen(Qt::NoPen);
                    pPainter->setBrush(oColors.m_oSection);
                    pPainter->drawRoundedRect(oTrack, 1, 1);
                    if (nValue > 0)
                    {
                        pPainter->setBrush(oColors.m_oAccent);
                        pPainter->drawRoundedRect(
                            QRect(oTrack.topLeft(), QSize(qMax(3, oTrack.width() * nValue / 100), 3)),
                            1, 1);
                    }
                }
                pPainter->restore();
            }
        };
    } // namespace
    ClientWindow::ClientWindow(const QString& strDirectory, const QString& strOrigin,
                               const QString& strSessionDirectory)
        : m_strOrigin(strOrigin)
    {
        m_strDirectory = strDirectory.isEmpty() ? privateDownloadDirectory() : strDirectory;
        m_pStore = openStore(m_strDirectory, m_strOrigin);
        m_pReadingState = std::make_unique<ReadingState>(m_oSettings, m_strOrigin);
        m_pCovers = std::make_unique<CoverStore>(
            m_strOrigin, QDir(m_strDirectory).filePath(".ui-cache"), nullptr);
        connect(m_pCovers.get(), &CoverStore::ready, this,
                [this](int nId, const QImage& oImage)
                {
                    if (hasLazyCovers(m_pCards))
                    {
                        m_pCards->viewport()->update();
                    }
                    else
                    {
                        for (int nI = 0; nI < m_pCards->count(); ++nI)
                        {
                            auto pItem = m_pCards->item(nI);
                            if (pItem->data(Qt::UserRole).toInt() == nId)
                            {
                                pItem->setData(Qt::UserRole + 3, oImage);
                                pItem->setData(Qt::UserRole + 6, oImage.isNull()
                                                                     ? m_pCovers->placeholder(nId)
                                                                     : QString());
                            }
                        }
                    }
                    if (m_oBook.m_nId == nId && m_pDetailCover)
                    {
                        if (!oImage.isNull())
                            m_pDetailCover->setPixmap(
                                coverPixmap(oImage, m_pDetailCover->size() - QSize(4, 6),
                                            m_pDetailCover->devicePixelRatioF()));
                        else
                            m_pDetailCover->setText(m_pCovers->placeholder(nId));
                    }
                    updateMobileContinueCover(nId, oImage);
                });
        connect(m_pCovers.get(), &CoverStore::statusChanged, this,
                [this](int nId)
                {
                    if (!hasLazyCovers(m_pCards))
                    {
                        for (int nI = 0; nI < m_pCards->count(); ++nI)
                        {
                            auto pItem = m_pCards->item(nI);
                            if (pItem->data(Qt::UserRole).toInt() == nId)
                                pItem->setData(Qt::UserRole + 6, m_pCovers->placeholder(nId));
                        }
                    }
                    if (m_oBook.m_nId == nId && m_pCovers->image(nId).isNull())
                        m_pDetailCover->setText(m_pCovers->placeholder(nId));
                });
        m_nFontSize = std::clamp(m_oSettings.value("fontSize", 18).toInt(), 10, 36);
        m_eTheme = Theme(std::clamp(m_oSettings.value("theme", 0).toInt(), 0, 2));
        setObjectName("clientWindow");
        setWindowTitle(applicationName());
        resize(1280, 860);
        setMinimumSize(320, 360);
        build();
        m_pReader->setReadingFont(m_oSettings.value("readerFontFamily").toString(),
                                  m_oSettings.value("readerBold", false).toBool());
        m_pReader->setComfortWidth(m_oSettings.value("readerComfortWidth", 0).toInt());
        m_pReader->setTypography(m_oSettings.value("readerLineHeight", 145).toInt(),
                                m_oSettings.value("readerParagraphSpacing", 8).toInt(),
                                m_oSettings.value("readerSideMargin", 0).toInt());
        m_bVolumePaging = m_oSettings.value("volumePageKeys", true).toBool();
        m_bKeepAwake = m_oSettings.value("readerKeepAwake", false).toBool();
        const QPointer<ClientWindow> pThis(this);
        setAndroidSafeInsetsChangedHandler([pThis]
        {
            if (pThis)
            {
                pThis->adaptLayout();
            }
        });
        setReadingMode(ReadingMode(qBound(0, m_oSettings.value("readingMode", 0).toInt(), 1)));
        initializeReadAloud();
        setTheme(m_eTheme);
        adaptLayout();
        m_pToast = new Toast(m_pRoot);
#ifdef WENKU8_CLIENT_ONLINE
        try
        {
            m_pSession = new OnlineSession(this, m_strOrigin, m_strDirectory, strSessionDirectory);
        }
        catch (const std::exception&)
        {
            // A missing platform browser must not block the local library.
        }
        if (m_pSession)
        {
            m_pSession->setTheme(m_eTheme);
            m_pDownloadTasks = new DownloadTaskPanel(m_pSession->downloads());
            static_cast<QVBoxLayout*>(m_pCards->parentWidget()->layout())->insertWidget(0, m_pDownloadTasks);
            m_pDownloadTasks->hide();
            connect(m_pSession->downloads(), &DownloadManager::completed, this,
                [this](int nId, bool bLocal, const QString& strLocation)
                {
                    if (bLocal)
                    {
                        m_oSettings.setValue("lastLocalDownload", strLocation);
                    }
                    else
                    {
                        m_pCovers->retryFailures();
                        requestCover(nId);
                        if (m_nSection == 2 && m_strActiveSearch.isEmpty() &&
                            m_pPages->currentIndex() == 1 && !m_bLoadingBusy)
                        {
                            downloadedBooks();
                        }
                    }
                    m_pToast->showText(bLocal ? "小说已保存到所选目录" : "下载缓存完成，可以离线阅读", m_eTheme);
                });
            connect(m_pSession->downloads(), &DownloadManager::notice, this,
                [this](const QString& strMessage)
                {
                    m_pToast->showText(strMessage, m_eTheme);
                });
            connect(m_pSession, &OnlineSession::restorationChanged, this,
                    [this]
                    {
                        updateAccountControls();
                    });
            connect(m_pSession, &OnlineSession::authenticationChanged, this, &ClientWindow::authenticate);
            connect(m_pSession, &OnlineSession::busyChanged, this,
                    [this](bool bValue)
                    {
                        if (bValue)
                            m_nActiveRequestContext = m_nBusinessContext;
                        else
                            finishDiscoveryLoad();
                        updateBusy(bValue);
                        if (!bValue && m_nSpeechPendingBook != 0)
                        {
                            QTimer::singleShot(0, this, [this]
                            {
                                if (m_nSpeechPendingBook != 0 && !m_bLoadingBusy &&
                                    m_pReadAloud->state() == ReadAloudController::State::WaitingChapter)
                                {
                                    cancelSpeechChapter();
                                    m_pReadAloud->pause("章节加载失败，检查网络后可继续听书");
                                }
                            });
                        }
                    });
            connect(m_pSession, &OnlineSession::retryAvailableChanged, this,
                    [this](bool bAvailable)
                    {
                        if (bAvailable)
                            m_nRetryContext = m_nActiveRequestContext;
                        updateNetworkActions();
                        if (bAvailable && m_nDiscoveryPendingPage == 0 &&
                            m_pPages->currentIndex() == 1 && m_pCards->count() > 0)
                            m_pCount->setText(
                                QString("暂未更新 · 保留上次列表（%1 本）").arg(m_pCards->count()));
                    });
            connect(m_pSession, &OnlineSession::chapterImagesReady, this,
                    [this](int nNovel, int nChapter)
                    {
                        if (m_bAuthenticated && !m_bLocalBook && m_oBook.m_nId == nNovel &&
                            m_nChapter >= 0 && m_nChapter < m_vFlat.size() &&
                            m_vFlat[m_nChapter].m_nId == nChapter)
                            m_pReader->refreshImages();
                    });
            connect(m_pSession, &OnlineSession::progressChanged, this,
                    [this](qint64 nCurrent, qint64 nTotal)
                    {
                        if (m_bLoadingBusy)
                            m_pLoading->setProgress(nCurrent, nTotal);
                    });
            connect(m_pSession, &OnlineSession::statusChanged, this,
                    [this](const QString& strValue)
                    {
                        message(strValue);
                    });
            connect(m_pSession, &OnlineSession::bookshelfReady, this,
                    [this]
                    {
                        m_vShelfIds.clear();
                        for (const auto& oNovel : m_pSession->novels())
                            m_vShelfIds.insert(oNovel.m_nId);
                        updateBookActions();
                    });
            connect(m_pSession, &OnlineSession::bookshelfAdded, this,
                    [this](int nId)
                    {
                        m_vShelfIds.insert(nId);
                        updateBookActions();
                    });
            connect(m_pSession, &OnlineSession::novelsReady, this,
                    [this](int nDiscoveryPage, int nDiscoveryLastPage, bool bStale)
                    {
                        receiveLibraryNovels(nDiscoveryPage, nDiscoveryLastPage, bStale);
                        if (m_nStartupNovel && m_bAuthenticated && m_pPages->currentIndex() == 1)
                        {
                            int nId = m_nStartupNovel;
                            m_nStartupNovel = 0;
                            const auto nContext = m_nBusinessContext;
                            QTimer::singleShot(0, this, [this, nId, nContext]
                            {
                                if (nContext == m_nBusinessContext && m_pPages->currentIndex() == 1)
                                {
                                    openBook(nId);
                                }
                            });
                        }
                    });
            connect(m_pSession, &OnlineSession::bookReady, this,
                    [this]
                    {
                        showBook(m_pSession->novel(), m_pSession->volumes(), false);
                    });
            connect(m_pSession, &OnlineSession::chapterReady, this,
                    [this]
                    {
                        renderChapter(m_pSession->chapterText());
                    });

        }
#endif
        navigate(0);
        auto pScrollSave = new QTimer(this);
        pScrollSave->setSingleShot(true);
        pScrollSave->setInterval(250);
        connect(m_pReader->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this, pScrollSave]
                {
                    if (!m_bRestoring && m_pPages->currentIndex() == 3)
                    {
                        updateReaderProgress();
                        pScrollSave->start();
                    }
                });
        connect(pScrollSave, &QTimer::timeout, this,
                [this, pScrollSave]
                {
                    if (QScroller::scroller(m_pReader->viewport())->state() != QScroller::Inactive)
                        pScrollSave->start();
                    else
                        saveProgress();
                });
        auto pTimer = new QTimer(this);
        connect(pTimer, &QTimer::timeout, this,
                [this]
                {
                    if (QScroller::scroller(m_pReader->viewport())->state() == QScroller::Inactive)
                        saveProgress();
                });
        pTimer->start(3000);
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState eState)
                {
                    if (eState != Qt::ApplicationActive)
                    {
                        saveProgress();
                        saveSpeechPosition();
                        if (m_pReadAloud &&
                            (m_pReadAloud->state() == ReadAloudController::State::Playing ||
                             m_pReadAloud->state() == ReadAloudController::State::WaitingChapter))
                        {
                            cancelSpeechChapter();
                            m_pReadAloud->pause("应用已进入后台，听书已暂停");
                        }
                        m_oSettings.sync();
                    }
                    updateVolumePaging();
                });
        if (auto pScreen = QGuiApplication::primaryScreen())
            connect(pScreen, &QScreen::availableGeometryChanged, this,
                    [this]
                    {
                        adaptLayout();
                    });
    }
    ClientWindow::~ClientWindow()
    {
        ++m_nRestoreGeneration;
        cancelSpeechChapter();
        saveSpeechPosition();
        delete m_pReadAloud;
        m_pReadAloud = nullptr;
        setAndroidVolumePaging(false);
        setAndroidReadingActive(false, false);
        setAndroidSafeInsetsChangedHandler({});
        saveProgress();
        m_oSettings.sync();
#ifdef WENKU8_CLIENT_ONLINE
        delete m_pSession;
#endif
    }
    void ClientWindow::build()
    {
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        m_pDesktopMenuBar = new QMenuBar(this);
        m_pDesktopMenuBar->setObjectName("desktopMenuBar");
        setMenuBar(m_pDesktopMenuBar);
        auto pHelp = m_pDesktopMenuBar->addMenu("帮助");
        auto* pKeys = pHelp->addAction("键盘快捷键");
        connect(pKeys, &QAction::triggered, this, [this]
        {
            showInformationDocument("键盘快捷键",
                "书库\nCtrl+K：搜索书名\n搜索框中按 ↓：选中结果\n"
                "方向键：移动选中项\nEnter：打开选中的小说\nAlt+1 至 Alt+4：切换书库栏目\n\n"
                "阅读\nCtrl+F：章内查找\nF3 / Shift+F3：下一处 / 上一处\n"
                "Ctrl+D：添加当前位置书签\nAlt+← / Alt+→：上一章 / 下一章\n"
                "Ctrl+Shift+S：开关阅读侧栏\n目录筛选框中按 Enter：打开第一项\n"
                "PageUp / PageDown、← / →：正文聚焦时翻页\nF11：切换全屏\nEsc：关闭侧栏或退出全屏");
        });
        const QStringList vInfoTitles{"版本说明", "更新日志", "开源许可"};
        const char* vActionNames[] = {"aboutVersion", "aboutChangelog", "aboutLicenses"};
        for (int nI = 0; nI < vInfoTitles.size(); ++nI)
        {
            auto pInfo = pHelp->addAction(vInfoTitles[nI]);
            pInfo->setObjectName(vActionNames[nI]);
            connect(pInfo, &QAction::triggered, this,
                    [this, nI]
                    {
                        showAppInformation(nI);
                    });
        }
        auto* pRepository = pHelp->addAction("GitHub 开源项目");
        auto* pFeedback = pHelp->addAction("意见反馈");
        connect(pRepository, &QAction::triggered, this, []
        {
            QDesktopServices::openUrl(applicationRepositoryUrl());
        });
        connect(pFeedback, &QAction::triggered, this, []
        {
            QDesktopServices::openUrl(applicationFeedbackUrl());
        });
        auto* pDownloadSettings = pHelp->addAction("下载与存储");
        connect(pDownloadSettings, &QAction::triggered, this, &ClientWindow::showDownloadSettings);
        pHelp->addSeparator();
        auto pNotices = pHelp->addAction("完整第三方版权声明");
        pNotices->setObjectName("aboutThirdParty");
        connect(pNotices, &QAction::triggered, this,
                [this]
                {
                    showDesktopNotices();
                });
#endif
        m_pRoot = new QWidget;
        setCentralWidget(m_pRoot);
        auto* pRootLayout = new QVBoxLayout(m_pRoot);
        pRootLayout->setContentsMargins(0, 0, 0, 0);
        auto pShell = new QWidget;
        pShell->setObjectName("shell");
        pRootLayout->addWidget(pShell);
        auto pOuter = new QHBoxLayout(pShell);
        pOuter->setContentsMargins(0, 0, 0, 0);
        pOuter->setSpacing(0);
        auto pSidebar = new QFrame;
        m_pSidebar = pSidebar;
        pSidebar->setObjectName("sidebar");
        pSidebar->setFixedWidth(216);
        auto pSide = new QVBoxLayout(pSidebar);
        pSide->setContentsMargins(20, 30, 20, 24);
        pSide->setSpacing(8);
        pSide->addWidget(label(applicationName(), "brand"));
        pSide->addSpacing(20);
        auto* pNavigationGroup = new QButtonGroup(pSidebar);
        QStringList vNames{"我的书架", "最近更新", "下载缓存", "阅读历史"};
        for (int nI = 0; nI < vNames.size(); ++nI)
        {
            auto pB = button(vNames[nI], "nav");
            pB->setCheckable(true);
            pNavigationGroup->addButton(pB);
            setButtonIcon(pB, nI == 0   ? Icon::Library
                              : nI == 1 ? Icon::Discover
                              : nI == 2 ? Icon::Download
                                        : Icon::History);
            pSide->addWidget(pB);
            m_vNav.append(pB);
            connect(pB, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        navigate(nI);
                    });
        }
        pSide->addStretch();
        pSide->addWidget(label("外观", "eyebrow"));
        m_pThemes = new ThemeSelector;
        m_pThemes->setObjectName("themeSelector");
        m_pThemes->addItems({"明亮", "暗色", "护眼"});
        pSide->addWidget(m_pThemes);
        connect(m_pThemes, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int nI)
                {
                    setTheme(Theme(nI));
                });
        pSide->addSpacing(18);
        m_pDesktopAccount = button("我的", "desktopMyPage");
        m_pDesktopAccount->setCheckable(true);
        setButtonIcon(m_pDesktopAccount, Icon::Profile);
        pNavigationGroup->addButton(m_pDesktopAccount);
        pSide->addWidget(m_pDesktopAccount);
        connect(m_pDesktopAccount, &QPushButton::clicked, this, &ClientWindow::showAccountPage);
        pOuter->addWidget(pSidebar);
        auto pContent = new QWidget;
        auto pBody = new QVBoxLayout(pContent);
        m_pBodyLayout = pBody;
        pBody->setContentsMargins(36, 30, 36, 18);
        pBody->setSpacing(20);
        pOuter->addWidget(pContent, 1);
        m_pMobileHeader = new QWidget;
        m_pMobileHeader->setObjectName("mobileHeader");
        auto pMobileTop = new QHBoxLayout(m_pMobileHeader);
        pMobileTop->setContentsMargins(0, 0, 0, 0);
        auto pMobileBack = button({}, "mobileBack");
        pMobileBack->setAccessibleName("返回");
        pMobileBack->setToolTip("返回");
        pMobileBack->setFixedWidth(48);
        setButtonIcon(pMobileBack, Icon::Left);
        pMobileBack->setProperty("textAction", true);
        pMobileBack->setProperty("keepIcon", true);
        pMobileTop->addWidget(pMobileBack);
        connect(pMobileBack, &QPushButton::clicked, this,
                [this]
                {
                    goBack();
                });
        m_pMobileTitle = new ElidedLabel;
        m_pMobileTitle->setObjectName("mobilePageTitle");
        m_pMobileTitle->setText(applicationName());
        m_pMobileTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pMobileTop->addWidget(m_pMobileTitle, 1);
        pBody->addWidget(m_pMobileHeader);
        m_pMobileHeader->hide();
        m_pHeading = new QWidget;
        auto pHeading = new QHBoxLayout(m_pHeading);
        pHeading->setContentsMargins(0, 0, 0, 0);
        auto pTitles = new QVBoxLayout;
        pTitles->setSpacing(7);
        m_pTitle = label("欢迎来到文库", "pageTitle");
        m_pSubtitle = label("登录后，书架与下一段故事都在这里。", "subtitle");
        m_pSubtitle->setTextFormat(Qt::PlainText);
        m_pTitle->setWordWrap(true);
        m_pSubtitle->setWordWrap(true);
        pTitles->addWidget(m_pTitle);
        pTitles->addWidget(m_pSubtitle);
        pHeading->addLayout(pTitles, 1);
        m_pRefresh = button("刷新", "refreshButton");
        setButtonIcon(m_pRefresh, Icon::Refresh);
        pHeading->addWidget(m_pRefresh, 0, Qt::AlignTop);
        connect(m_pRefresh, &QPushButton::clicked, this,
                [this]
                {
                    refreshCurrentPage();
                });
        pBody->addWidget(m_pHeading);
        m_pOperations = new QWidget;
        auto pSearchRow = new QHBoxLayout(m_pOperations);
        pSearchRow->setContentsMargins(0, 0, 0, 0);
        pSearchRow->setSpacing(10);
        m_pSearch = new SearchInput("搜索书名");
        m_pSearch->setObjectName("novelSearch");
        m_pSearch->setClearButtonEnabled(true);
        m_pSearch->installEventFilter(this);
        pSearchRow->addWidget(m_pSearch, 1);
        auto pSearchButton = button("搜索", "searchNovels");
        pSearchButton->setProperty("controlAction", true);
        setButtonIcon(pSearchButton, Icon::Search);
        pSearchRow->addWidget(pSearchButton);
        pBody->addWidget(m_pOperations);
        auto fnDoSearch = [this]
        {
            const auto strQuery = m_pSearch->text().trimmed();
            if (!m_bAuthenticated || m_bLoadingBusy || strQuery.isEmpty())
                return;
            ++m_nBusinessContext;
            m_oLibraryReturn = {};
            if (m_strActiveSearch != strQuery)
            {
                resetDiscoveryFeed();
                m_pCards->clear();
            }
            if (mobileMode())
            {
                m_pSearch->clearFocus();
                QGuiApplication::inputMethod()->hide();
            }
            m_pCount->setText("正在搜索…");
            m_strActiveSearch = strQuery;
            updateNetworkActions();
            saveProgress();
            m_pTitle->setText("搜索结果");
            m_pSubtitle->setText(QString("关键词「%1」").arg(strQuery));
            m_pPages->setCurrentIndex(1);
            adaptLayout();
#ifdef WENKU8_CLIENT_ONLINE
            if (m_pSession)
                m_pSession->search(strQuery);
#endif
        };
        connect(pSearchButton, &QPushButton::clicked, this, fnDoSearch);
        connect(m_pSearch, &QLineEdit::returnPressed, this, fnDoSearch);
        m_pPages = new AnimatedStack;
        m_pPages->setObjectName("contentPages");
        pBody->addWidget(m_pPages, 1);
        connect(m_pPages, &QStackedWidget::currentChanged, this,
                [this]
                {
                    if (m_pReader)
                        adaptLayout();
                    if (m_pDetailProgress && m_pPages->currentIndex() == 2)
                    {
                        updateBookDetailProgress();
                    }
                    if (m_pLoading)
                        m_pLoading->setGeometry(m_pRoot->rect());
                });
        auto pLocked = new QFrame;
        pLocked->setObjectName("guestLibraryGate");
        auto pGate = new QVBoxLayout(pLocked);
        pGate->setContentsMargins(16, 24, 16, 24);
        pGate->addStretch();
        m_pGuestTitle = label("登录后查看书架", "bookTitle");
        m_pGuestTitle->setWordWrap(true);
        pGate->addWidget(m_pGuestTitle);
        auto* pGuestHint = label("可直接打开已下载的作品，离线继续阅读。", "muted");
        pGuestHint->setWordWrap(true);
        pGate->addWidget(pGuestHint);
        pGate->addSpacing(14);
        auto* pOpenMy = button("前往我的", "openMyPage");
        auto* pOpenCache = button("打开下载缓存", "openOfflineCache");
        setButtonIcon(pOpenMy, Icon::Profile);
        setButtonIcon(pOpenCache, Icon::Download);
        pGate->addWidget(pOpenMy, 0, Qt::AlignLeft);
        pGate->addWidget(pOpenCache, 0, Qt::AlignLeft);
        connect(pOpenMy, &QPushButton::clicked, this, &ClientWindow::showAccountPage);
        connect(pOpenCache, &QPushButton::clicked, this,
                [this]
                {
                    navigate(2);
                });
        pGate->addStretch();
        auto* pWelcomeLinks = label(
            "<a href=\"" + applicationRepositoryUrl().toString() + "\">开源项目</a> · "
            "<a href=\"" + applicationFeedbackUrl().toString() + "\">意见反馈</a>", "welcomeLinks");
        pWelcomeLinks->setTextFormat(Qt::RichText);
        pWelcomeLinks->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        connect(pWelcomeLinks, &QLabel::linkActivated, this, [](const QString& strLink)
        {
            const QUrl oUrl(strLink);
            if (isApplicationLink(oUrl))
            {
                QDesktopServices::openUrl(oUrl);
            }
        });
        pGate->addWidget(pWelcomeLinks);
        m_pPages->addWidget(pLocked);
        auto pLibrary = new QWidget;
        auto pLibraryLayout = new QVBoxLayout(pLibrary);
        pLibraryLayout->setContentsMargins(0, 0, 0, 0);
        auto pLibraryBar = new QHBoxLayout;
        m_pCount = label("正在准备你的书库", "muted");
        m_pCount->setObjectName("listCount");
        m_pCount->setWordWrap(true);
        m_pCount->setMinimumWidth(0);
        pLibraryBar->addWidget(m_pCount, 1);
        buildMobileLibrary(pLibraryLayout, pLibraryBar);
        buildDesktopLibrary(pLibraryBar);
        pLibraryLayout->addLayout(pLibraryBar);
        m_pCards = new QListWidget;
        m_pCards->setObjectName("cards");
        m_pCards->setViewMode(QListView::IconMode);
        m_pCards->setResizeMode(QListView::Adjust);
        m_pCards->setMovement(QListView::Static);
        m_pCards->setWrapping(true);
        m_pCards->setMouseTracking(true);
        m_pCards->setSpacing(6);
        m_pCards->setUniformItemSizes(true);
        m_pCards->setCursor(Qt::PointingHandCursor);
        m_pCards->installEventFilter(this);
        m_pCards->setItemDelegate(new BookCards(m_pCards, m_pCovers.get(),
                                               [this](int nId)
                                               {
                                                   requestCover(nId);
                                               }));
        pLibraryLayout->addWidget(m_pCards, 1);
        connect(m_pCards, &QListWidget::itemClicked, this,
                [this](QListWidgetItem* pItem)
                {
                    openBook(pItem->data(Qt::UserRole).toInt(),
                             m_nSection == 3 && m_strActiveSearch.isEmpty());
                });
        m_pPages->addWidget(pLibrary);
        wireLibraryFeed();
        auto pDetail = new QFrame;
        pDetail->setObjectName("surface");
        auto pInfo = new QVBoxLayout(pDetail);
        m_pDetailLayout = pInfo;
        pInfo->setContentsMargins(32, 28, 32, 28);
        pInfo->setSpacing(16);
        auto pBack = button("返回书库", "detailBack");
        setButtonIcon(pBack, Icon::Left);
        pBack->setMaximumWidth(140);
        pInfo->addWidget(pBack);
        connect(pBack, &QPushButton::clicked, this,
                [this]
                {
                    goBack();
                });
        m_pDetailColumns = new QBoxLayout(QBoxLayout::TopToBottom);
        pInfo->addLayout(m_pDetailColumns, 1);
        auto* pOverview = new QWidget;
        auto* pOverviewLayout = new QVBoxLayout(pOverview);
        pOverviewLayout->setContentsMargins(0, 0, 0, 0);
        pOverviewLayout->setSpacing(8);
        m_pDetailColumns->addWidget(pOverview);
        auto* pHero = new QWidget;
        pHero->setObjectName("bookDetailHero");
        pHero->setAttribute(Qt::WA_StyledBackground);
        auto pDetailHeader = new QHBoxLayout(pHero);
        m_pDetailHeaderLayout = pDetailHeader;
        pDetailHeader->setSpacing(26);
        m_pDetailCover = label("加载封面…", "coverImage");
        m_pDetailCover->setFixedSize(132, 192);
        m_pDetailCover->setAlignment(Qt::AlignCenter);
        m_pDetailCover->setWordWrap(true);
        pDetailHeader->addWidget(m_pDetailCover, 0, Qt::AlignTop);
        auto pDetailWords = new QVBoxLayout;
        m_pDetailTitle = new ElidedLabel(2);
        m_pDetailTitle->setObjectName("bookTitle");
        m_pDetailTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pDetailMeta = new ElidedLabel;
        m_pDetailMeta->setObjectName("muted");
        m_pDetailMeta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pDetailIntro = new ElidedLabel(3);
        m_pDetailIntro->setObjectName("intro");
        m_pDetailIntro->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pDetailProgressText = new ElidedLabel;
        m_pDetailProgressText->setObjectName("detailProgressText");
        m_pDetailProgressText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pDetailProgress = new QProgressBar;
        m_pDetailProgress->setObjectName("detailReadingProgress");
        m_pDetailProgress->setAccessibleName("上次阅读章节的进度");
        m_pDetailProgress->setRange(0, 100);
        m_pDetailProgress->setTextVisible(false);
        m_pDetailProgress->setFixedHeight(4);
        pDetailWords->addWidget(m_pDetailTitle);
        pDetailWords->addWidget(m_pDetailMeta);
        pDetailWords->addWidget(m_pDetailProgressText);
        pDetailWords->addWidget(m_pDetailProgress);
        pDetailWords->addWidget(m_pDetailIntro);
        pDetailWords->addStretch();
        pDetailHeader->addLayout(pDetailWords, 1);
        pOverviewLayout->addWidget(pHero);
        auto pBookActions = new QGridLayout;
        m_pDetailActions = pBookActions;
        pBookActions->setHorizontalSpacing(8);
        pBookActions->setVerticalSpacing(4);
        m_pRead = button("开始阅读", "primary");
        m_pRead->setObjectName("readBook");
        m_pRead->setProperty("primary", true);
        setButtonIcon(m_pRead, Icon::Book);
        m_pDownload = button("下载缓存", "downloadBook");
        m_pLocalDownload = button("下载到本地", "downloadLocalBook");
        setButtonIcon(m_pLocalDownload, Icon::Folder);
        setButtonIcon(m_pDownload, Icon::Download);
        m_pShelfAdd = button("加入书架", "addToShelf");
        setButtonIcon(m_pShelfAdd, Icon::ShelfAdd);
        m_pIllustrations = new DownloadOption;
        m_pIllustrations->setChecked(true);
        pBookActions->addWidget(m_pRead, 0, 0);
        pBookActions->addWidget(m_pDownload, 1, 0);
        pBookActions->addWidget(m_pLocalDownload, 1, 1);
        pBookActions->addWidget(m_pShelfAdd, 0, 1);
        pBookActions->addWidget(m_pIllustrations, 1, 2);
        auto pSynopsis = button("小说简介", "showSynopsis");
        setButtonIcon(pSynopsis, Icon::Book);
        pBookActions->addWidget(pSynopsis, 0, 2);
        for (auto pAction : {m_pDownload, m_pLocalDownload, m_pShelfAdd, pSynopsis})
        {
            pAction->setProperty("controlAction", true);
            pAction->setProperty("toolbarAction", true);
        }
        connect(pSynopsis, &QPushButton::clicked, this,
                [this]
                {
                    const QString strSynopsis = text(m_oBook.m_strIntro).trimmed();
                    if (mobileMode())
                    {
                        showMobileText("小说简介", strSynopsis);
                        return;
                    }
                    QDialog oDialog(this);
                    oDialog.setObjectName("synopsisDialog");
                    oDialog.setWindowTitle("小说简介");
                    auto pLayout = new QVBoxLayout(&oDialog);
                    auto pTextView = new QTextBrowser;
                    pTextView->setPlainText(strSynopsis);
                    pLayout->addWidget(pTextView, 1);
                    auto pClose = button("关闭");
                    pClose->setMinimumHeight(44);
                    pLayout->addWidget(pClose);
                    connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
                    applyTheme(&oDialog, m_eTheme);
                    oDialog.resize(qMin(560, width() - 24), qMin(500, height() - 40));
                    oDialog.exec();
                });
        pOverviewLayout->addLayout(pBookActions);
        pOverviewLayout->addStretch(1);
        auto* pCatalog = new QWidget;
        auto* pCatalogLayout = new QVBoxLayout(pCatalog);
        pCatalogLayout->setContentsMargins(0, 0, 0, 0);
        pCatalogLayout->setSpacing(6);
        m_pDetailColumns->addWidget(pCatalog, 1);
        auto* pCatalogHeader = new QHBoxLayout;
        m_pDetailChapterHeading = label("章节目录", "controlLabel");
        m_pDetailChapterHeading->setObjectName("detailChapterHeading");
        m_pDetailChapterHeading->setWordWrap(true);
        pCatalogHeader->addWidget(m_pDetailChapterHeading, 1);
        m_pDetailFilterToggle = button("查找章节", "detailFilterToggle");
        m_pDetailFilterToggle->setCheckable(true);
        m_pDetailFilterToggle->setProperty("controlAction", true);
        m_pDetailFilterToggle->setProperty("toolbarAction", true);
        setButtonIcon(m_pDetailFilterToggle, Icon::Search);
        pCatalogHeader->addWidget(m_pDetailFilterToggle);
        pCatalogLayout->addLayout(pCatalogHeader);
        m_pDetailFilter = new QLineEdit;
        m_pDetailFilter->setObjectName("detailChapterFilter");
        m_pDetailFilter->setAccessibleName("按卷名或章名筛选目录");
        m_pDetailFilter->setPlaceholderText("输入卷名或章名");
        m_pDetailFilter->setClearButtonEnabled(true);
        m_pDetailFilter->hide();
        pCatalogLayout->addWidget(m_pDetailFilter);
        m_pToc = new QTreeWidget;
        m_pToc->setObjectName("chapterTree");
        m_pToc->setHeaderHidden(true);
        m_pToc->setColumnCount(2);
        m_pToc->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_pToc->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_pToc->setMouseTracking(true);
        m_pToc->setIndentation(24);
        m_pToc->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_pToc->setItemDelegate(new ChapterRows(m_pToc));
        pCatalogLayout->addWidget(m_pToc, 1);
        m_pPages->addWidget(pDetail);
        connect(m_pDetailFilterToggle, &QPushButton::toggled, this, [this](bool bOpen)
        {
            m_pDetailFilter->setVisible(bOpen);
            if (bOpen)
            {
                m_pDetailFilter->setFocus(Qt::OtherFocusReason);
                if (mobileMode())
                {
                    QGuiApplication::inputMethod()->show();
                }
            }
            else
            {
                m_pDetailFilter->clear();
                QGuiApplication::inputMethod()->hide();
            }
        });
        connect(m_pDetailFilter, &QLineEdit::textChanged, this, &ClientWindow::updateBookDetailFilter);
        connect(m_pDetailFilter, &QLineEdit::returnPressed, this, [this]
        {
            if (!m_bDetailFiltering || m_pPages->currentIndex() != 2 || m_bLoadingBusy)
            {
                return;
            }
            for (int nVolume = 0; nVolume < m_pToc->topLevelItemCount(); ++nVolume)
            {
                auto* pVolume = m_pToc->topLevelItem(nVolume);
                if (!pVolume->isHidden())
                {
                    for (int nChapter = 0; nChapter < pVolume->childCount(); ++nChapter)
                    {
                        auto* pChapter = pVolume->child(nChapter);
                        if (!pChapter->isHidden())
                        {
                            QGuiApplication::inputMethod()->hide();
                            openChapter(pChapter->data(0, Qt::UserRole).toInt());
                            return;
                        }
                    }
                }
            }
        });
        connect(m_pRead, &QPushButton::clicked, this,
                [this]
                {
                    auto oSaved = m_pReadingState->position(m_oBook.m_nId);
                    int nWanted = oSaved ? oSaved->m_nChapter : 0;
                    for (int nI = 0; nI < m_vFlat.size(); ++nI)
                        if (m_vFlat[nI].m_nId == nWanted)
                        {
                            openChapter(nI);
                            return;
                        }
                    openChapter(0);
                });
        connect(m_pShelfAdd, &QPushButton::clicked, this,
                [this]
                {
#ifdef WENKU8_CLIENT_ONLINE
                    if (m_bAuthenticated && m_pSession && m_oBook.m_nId > 0 &&
                        !m_vShelfIds.contains(m_oBook.m_nId))
                        m_pSession->addToShelf(m_oBook.m_nId);
#endif
                });
        connect(m_pDownload, &QPushButton::clicked, this, &ClientWindow::cacheBook);
        connect(m_pLocalDownload, &QPushButton::clicked, this, &ClientWindow::downloadBookToLocal);
        connect(m_pToc, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem* pItem, int)
                {
                    if (m_pPages->currentIndex() != 2 || m_bLoadingBusy)
                    {
                        return;
                    }
                    auto oValue = pItem->data(0, Qt::UserRole);
                    if (oValue.isValid())
                    {
                        QGuiApplication::inputMethod()->hide();
                        openChapter(oValue.toInt());
                    }
                    else
                        pItem->setExpanded(!pItem->isExpanded());
                });
        connect(m_pToc, &QTreeWidget::itemActivated, this,
                [this](QTreeWidgetItem* pItem, int)
                {
                    auto oValue = pItem->data(0, Qt::UserRole);
                    if (oValue.isValid() && m_pPages->currentIndex() == 2 && !m_bLoadingBusy)
                    {
                        QGuiApplication::inputMethod()->hide();
                        openChapter(oValue.toInt());
                    }
                });
        connect(m_pToc, &QTreeWidget::itemExpanded, this,
                [](QTreeWidgetItem* pItem)
                {
                    pItem->setText(1, QString("%1 章 · 点击收起").arg(pItem->childCount()));
                });
        connect(m_pToc, &QTreeWidget::itemCollapsed, this,
                [](QTreeWidgetItem* pItem)
                {
                    pItem->setText(1, QString("%1 章 · 点击展开").arg(pItem->childCount()));
                });
        connect(m_pToc, &QTreeWidget::entered, this,
                [this](const QModelIndex&)
                {
                    m_pToc->viewport()->setCursor(m_pToc->isEnabled() ? Qt::PointingHandCursor
                                                                      : Qt::ArrowCursor);
                });
        auto pReading = new QWidget;
        auto pReadingLayout = new QVBoxLayout(pReading);
        pReadingLayout->setContentsMargins(0, 0, 0, 0);
        pReadingLayout->setSpacing(12);
        auto pReaderTopHost = new QWidget;
        pReaderTopHost->setObjectName("readerTop");
        pReaderTopHost->setAttribute(Qt::WA_StyledBackground);
        auto pReaderTopLayout = new QHBoxLayout(pReaderTopHost);
        pReaderTopLayout->setContentsMargins(0, 0, 0, 0);
        auto pReaderBack = button("返回", "readerBack");
        setButtonIcon(pReaderBack, Icon::Left);
        pReaderBack->setProperty("textAction", true);
        pReaderBack->setProperty("keepIcon", true);
        pReaderTopLayout->addWidget(pReaderBack);
        connect(pReaderBack, &QPushButton::clicked, this,
                [this]
                {
                    saveProgress();
                    leaveReaderFullscreen();
                    ++m_nRestoreGeneration;
                    m_bRestoring = false;
                    m_bReaderToolsOpen = false;
                    m_pTitle->setText("小说详情");
                    m_pSubtitle->setText("一段故事，从这里开始。");
                    m_pPages->setCurrentIndex(2);
                });
        auto* pReaderTitles = new QVBoxLayout;
        pReaderTitles->setContentsMargins(2, 0, 2, 0);
        pReaderTitles->setSpacing(2);
        m_pReadBook = new ElidedLabel;
        m_pReadBook->setObjectName("controlLabel");
        m_pReadBook->setAccessibleName("当前小说");
        m_pReadBook->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QFont oBookFont = m_pReadBook->font();
        oBookFont.setBold(true);
        m_pReadBook->setFont(oBookFont);
        pReaderTitles->addWidget(m_pReadBook);
        m_pReadTitle = new ElidedLabel;
        m_pReadTitle->setObjectName("muted");
        m_pReadTitle->setAccessibleName("当前阅读章节");
        m_pReadTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pReaderTitles->addWidget(m_pReadTitle);
        pReaderTopLayout->addLayout(pReaderTitles, 1);
        auto* pSidebarToggle = button("侧栏", "readerSidebarToggle");
        setButtonIcon(pSidebarToggle, Icon::List);
        pSidebarToggle->setToolTip("开关阅读侧栏（Ctrl+Shift+S）");
        pReaderTopLayout->addWidget(pSidebarToggle);
        connect(pSidebarToggle, &QPushButton::clicked, this, &ClientWindow::toggleDesktopReaderPanel);
        m_pFullscreen = button("全屏阅读", "readerFullscreen");
        setButtonIcon(m_pFullscreen, Icon::Fullscreen);
        m_pFullscreen->setToolTip("全屏阅读（F11），按 Esc 退出");
        m_pReaderToolsToggle = button("工具", "readerToolsToggle");
        setButtonIcon(m_pReaderToolsToggle, Icon::Settings);
        m_pReaderToolsToggle->setProperty("textAction", true);
        m_pReaderToolsToggle->setProperty("keepIcon", true);
        pReaderTopLayout->addWidget(m_pReaderToolsToggle);
        connect(m_pReaderToolsToggle, &QPushButton::clicked, this,
                [this]
                {
                    if (mobileMode())
                        showMobileReaderTools();
                    else
                    {
                        m_bReaderToolsOpen = !m_bReaderToolsOpen;
                        adaptLayout();
                    }
                });
        pReaderTopLayout->addWidget(m_pFullscreen);
        connect(m_pFullscreen, &QPushButton::clicked, this,
                [this]
                {
                    toggleReaderFullscreen();
                });
        auto pF11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
        connect(pF11, &QShortcut::activated, this,
                [this]
                {
                    if (m_pPages->currentIndex() == 3)
                        toggleReaderFullscreen();
                });
        auto pEscapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(pEscapeShortcut, &QShortcut::activated, this,
                [this]
                {
                    if (QApplication::activePopupWidget())
                    {
                        return;
                    }
                    if (!mobileMode() && m_pReaderDock && m_pReaderDock->isVisible())
                    {
                        closeReaderPanel();
                        return;
                    }
                    if (!m_vMobileHistory.isEmpty() || m_bCompact)
                    {
                        goBack();
                    }
                    else
                    {
                        leaveReaderFullscreen();
                    }
                });
        pReadingLayout->addWidget(pReaderTopHost);
        m_pReaderTools = new QWidget;
        m_pReaderTools->setObjectName("readerTools");
        auto pReaderTools = new FlowLayout;
        m_pReaderTools->setLayout(pReaderTools);
        auto pSmall = button("缩小"), pLarge = button("放大");
        setButtonIcon(pSmall, Icon::TextSmaller);
        setButtonIcon(pLarge, Icon::TextLarger);
        pReaderTools->addWidget(pSmall);
        pReaderTools->addWidget(pLarge);
        auto* pTypography = button("排版", "readerTypography");
        setButtonIcon(pTypography, Icon::Book);
        pReaderTools->addWidget(pTypography);
        connect(pTypography, &QPushButton::clicked, this, &ClientWindow::showMobileReaderTools);
        auto fnChangeFont = [this](int nDelta)
        {
            m_nFontSize = std::clamp(m_nFontSize + nDelta, 10, 36);
            m_pReader->appearance(m_nFontSize, m_eTheme);
            m_oSettings.setValue("fontSize", m_nFontSize);
        };
        connect(pSmall, &QPushButton::clicked, this,
                [fnChangeFont]
                {
                    fnChangeFont(-1);
                });
        connect(pLarge, &QPushButton::clicked, this,
                [fnChangeFont]
                {
                    fnChangeFont(1);
                });
        auto pMark = button("添加书签", "addBookmark"),
             pRestore = button("书签列表", "bookmarkList");
        setButtonIcon(pMark, Icon::Bookmark);
        setButtonIcon(pRestore, Icon::RestoreBookmark);
        pReaderTools->addWidget(pMark);
        pReaderTools->addWidget(pRestore);
        connect(pMark, &QPushButton::clicked, this,
                [this]
                {
                    addBookmark();
                });
        connect(pRestore, &QPushButton::clicked, this,
                [this]
                {
                    showBookmarks();
                });
        auto* pFindButton = button("章内查找", "readerSearch");
        setButtonIcon(pFindButton, Icon::Search);
        pReaderTools->addWidget(pFindButton);
        connect(pFindButton, &QPushButton::clicked, this, &ClientWindow::showReaderSearch);
        auto* pSpeech = button("听书", "readerSpeech");
        setButtonIcon(pSpeech, Icon::Headphones);
        pReaderTools->addWidget(pSpeech);
        connect(pSpeech, &QPushButton::clicked, this, &ClientWindow::showReadAloud);
        initializeDesktopReaderPanelShortcut();
        auto* pFindShortcut = new QShortcut(QKeySequence::Find, this);
        connect(pFindShortcut, &QShortcut::activated, this, [this]
        {
            if (m_pPages->currentIndex() == 3)
            {
                showReaderSearch();
            }
        });
        for (bool bBackward : {false, true})
        {
            auto* pRepeat = new QShortcut(QKeySequence(bBackward ? "Shift+F3" : "F3"), this);
            connect(pRepeat, &QShortcut::activated, this, [this, bBackward]
            {
                if (m_pPages->currentIndex() != 3 || !m_pReaderDock || !m_pReaderDock->isVisible())
                {
                    return;
                }
                if (auto* pAction = m_pReaderDock->findChild<QPushButton*>(
                        bBackward ? "readerSearchPrevious" : "readerSearchNext"))
                {
                    pAction->click();
                }
            });
            auto* pChapterKey = new QShortcut(QKeySequence(bBackward ? "Alt+Left" : "Alt+Right"), this);
            connect(pChapterKey, &QShortcut::activated, this, [this, bBackward]
            {
                if (m_pPages->currentIndex() == 3 && !m_bLoadingBusy && !m_bRestoring)
                {
                    openChapter(m_nChapter + (bBackward ? -1 : 1));
                }
            });
        }
        auto* pBookmarkShortcut = new QShortcut(QKeySequence("Ctrl+D"), this);
        connect(pBookmarkShortcut, &QShortcut::activated, this, [this]
        {
            if (m_pPages->currentIndex() == 3)
            {
                addBookmark();
            }
        });
        for (int nI = 0; nI < 2; ++nI)
        {
            auto pMode = button(nI == 0 ? "滚动阅读" : "翻页阅读", "readingModeChoice");
            pMode->setProperty("modeIndex", nI);
            pMode->setCheckable(true);
            pReaderTools->addWidget(pMode);
            connect(pMode, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        setReadingMode(ReadingMode(nI));
                    });
        }
        pReadingLayout->addWidget(m_pReaderTools);
        auto pPaper = new QFrame;
        pPaper->setObjectName("readingPaper");
        auto pCenter = new QHBoxLayout(pPaper);
        pCenter->setContentsMargins(0, 0, 0, 0);
        m_pReader = new ReadingView;
        m_pReader->setObjectName("readingView");
        m_pReader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        pCenter->addWidget(m_pReader, 1);
        pReadingLayout->addWidget(pPaper, 1);
        auto pFooterHost = new QWidget;
        pFooterHost->setObjectName("readerFooter");
        pFooterHost->setAttribute(Qt::WA_StyledBackground);
        auto pFooterLayout = new QVBoxLayout(pFooterHost);
        pFooterLayout->setContentsMargins(8, 6, 8, 8);
        pFooterLayout->setSpacing(2);
        pFooterLayout->addWidget(createSpeechMini());
        auto* pSeekRow = new QHBoxLayout;
        pSeekRow->setSpacing(4);
        m_pReaderPosition = label("本章", "readerProgress");
        m_pReaderPosition->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        pSeekRow->addWidget(m_pReaderPosition);
        m_pReaderProgress = new QSlider(Qt::Horizontal);
        m_pReaderProgress->setObjectName("readerProgressSlider");
        m_pReaderProgress->setAccessibleName("本章阅读进度");
        m_pReaderProgress->setToolTip("拖动预览位置，松开后跳转");
        m_pReaderProgress->setRange(0, 1000);
        m_pReaderProgress->setSingleStep(10);
        m_pReaderProgress->setPageStep(100);
        m_pReaderProgress->setMinimumHeight(48);
        m_pReaderProgress->setLayoutDirection(Qt::LeftToRight);
        pSeekRow->addWidget(m_pReaderProgress, 1);
        m_pReaderProgressPercent = label("0%", "readerProgress");
        m_pReaderProgressPercent->setMinimumWidth(70);
        m_pReaderProgressPercent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pSeekRow->addWidget(m_pReaderProgressPercent);
        pFooterLayout->addLayout(pSeekRow);
        auto fnSeekProgress = [this]
        {
            if (!canReadBook() || m_bLoadingBusy || m_bRestoring ||
                m_pPages->currentIndex() != 3 || m_nChapter < 0)
            {
                return;
            }
            if (m_pReadAloud)
            {
                m_pReadAloud->setFollow(false);
            }
            m_pReader->seekChapterProgress(m_pReaderProgress->value());
            updateReaderProgress();
            saveProgress();
        };
        connect(m_pReaderProgress, &QSlider::sliderPressed, m_pReader, &ReadingView::stopMotion);
        connect(m_pReaderProgress, &QSlider::sliderReleased, this, fnSeekProgress);
        connect(m_pReaderProgress, &QSlider::valueChanged, this, [this, fnSeekProgress](int nValue)
        {
            updateReaderPositionText(nValue);
            if (!m_pReaderProgress->isSliderDown())
            {
                fnSeekProgress();
            }
        });
        auto* pFooterGroup = new QWidget;
        pFooterGroup->setObjectName("controlGroup");
        auto pFooterActions = new QHBoxLayout(pFooterGroup);
        pFooterActions->setContentsMargins(4, 2, 4, 2);
        pFooterActions->setSpacing(8);
        m_pPrevious = button("上一章", "readerPrevious");
        m_pNext = button("下一章", "readerNext");
        setButtonIcon(m_pPrevious, Icon::Left);
        setButtonIcon(m_pNext, Icon::Right);
        m_pNext->setLayoutDirection(Qt::RightToLeft);
        m_pChapterLabel = label("", "readerProgress");
        m_pChapterLabel->setMinimumHeight(24);
        m_pChapterLabel->setAccessibleName("阅读进度");
        m_pChapterLabel->setAlignment(Qt::AlignCenter);
        m_pReaderContents = button("目录", "readerContents");
        setButtonIcon(m_pReaderContents, Icon::List);
        connect(m_pReaderContents, &QPushButton::clicked, this,
                [this]
                {
                    showReaderContents();
                });
        for (auto pAction : {m_pPrevious, m_pReaderContents, m_pNext})
        {
            pAction->setProperty("textAction", true);
            pAction->setProperty("keepIcon", true);
            pAction->setProperty("toolbarAction", true);
            pAction->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            pFooterActions->addWidget(pAction, 1);
        }
        m_pReaderActionLayout = new QBoxLayout(QBoxLayout::TopToBottom);
        m_pReaderActionLayout->setSpacing(6);
        m_pReaderActionLayout->addWidget(pFooterGroup, 3);
        pFooterLayout->addLayout(m_pReaderActionLayout);
        pFooterLayout->addWidget(m_pChapterLabel);
        auto* pQuickTools = new QWidget;
        pQuickTools->setObjectName("readerQuickTools");
        pQuickTools->setProperty("settingsStepper", true);
        pQuickTools->setAttribute(Qt::WA_StyledBackground);
        auto* pQuickLayout = new QHBoxLayout(pQuickTools);
        pQuickLayout->setContentsMargins(4, 2, 4, 2);
        pQuickLayout->setSpacing(4);
        auto* pQuickSearch = button("查找", "readerQuickSearch");
        auto* pQuickBookmarks = button("书签", "readerQuickBookmarks");
        auto* pQuickAppearance = button("外观", "readerQuickAppearance");
        auto* pQuickSpeech = button("听书", "readerQuickSpeech");
        setButtonIcon(pQuickSearch, Icon::Search);
        setButtonIcon(pQuickBookmarks, Icon::Bookmark);
        setButtonIcon(pQuickAppearance, Icon::Sun);
        setButtonIcon(pQuickSpeech, Icon::Headphones);
        for (auto* pAction : {pQuickSearch, pQuickBookmarks, pQuickAppearance, pQuickSpeech})
        {
            pAction->setAccessibleName(pAction->text());
            pAction->setToolTip(pAction->text());
            pAction->setProperty("textAction", true);
            pAction->setProperty("keepIcon", true);
            pAction->setProperty("toolbarAction", true);
            pAction->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            pQuickLayout->addWidget(pAction, 1);
            m_vReaderQuickActions.append(pAction);
        }
        connect(pQuickSearch, &QPushButton::clicked, this, &ClientWindow::showReaderSearch);
        connect(pQuickBookmarks, &QPushButton::clicked, this, &ClientWindow::showBookmarks);
        connect(pQuickAppearance, &QPushButton::clicked, this, &ClientWindow::showMobileAppearance);
        connect(pQuickSpeech, &QPushButton::clicked, this, &ClientWindow::showReadAloud);
        m_pReaderActionLayout->addWidget(pQuickTools, 4);
        pReadingLayout->addWidget(pFooterHost);
        m_pPages->addWidget(pReading);
        connect(m_pPrevious, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pReader->readingMode() == ReadingMode::Paged)
                        turnReaderPage(-1);
                    else
                        openChapter(m_nChapter - 1);
                });
        connect(m_pNext, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pReader->readingMode() == ReadingMode::Paged)
                        turnReaderPage(1);
                    else
                        openChapter(m_nChapter + 1);
                });
        connect(m_pReader, &ReadingView::pageStateChanged, this,
                [this]
                {
                    updateReaderFooter();
                });
        connect(m_pReader, &ReadingView::chapterBoundaryRequested, this,
                [this](int nDirection)
                {
                    turnReaderPage(nDirection);
                });
        m_pStatusArea = new QWidget;
        m_pStatusArea->setObjectName("connectionStatus");
        auto pStatusRow = new QHBoxLayout(m_pStatusArea);
        pStatusRow->setContentsMargins(0, 0, 0, 0);
        pStatusRow->setSpacing(8);
        m_pStatus = label("欢迎来到你的轻小说书房。", "status");
        m_pStatus->setWordWrap(true);
        pStatusRow->addWidget(m_pStatus, 1);
        m_pRetryNetwork = button("重新连接", "retryNetwork");
        setButtonIcon(m_pRetryNetwork, Icon::Refresh);
        m_pRetryNetwork->hide();
        pStatusRow->addWidget(m_pRetryNetwork);
        pBody->addWidget(m_pStatusArea);
        connect(m_pRetryNetwork, &QPushButton::clicked, this,
                [this]
                {
#ifdef WENKU8_CLIENT_ONLINE
                    if (isDiscoveryFeed() && m_bDiscoveryAppendFailed)
                    {
                        loadMoreDiscovery(true);
                    }
                    else if (m_pSession && m_nRetryContext == m_nBusinessContext)
                        m_pSession->retryLastRequest();
#endif
                });
        m_pMobileNavigation = new QWidget;
        m_pMobileNavigation->setObjectName("mobileNavigation");
        auto pTabs = new QHBoxLayout(m_pMobileNavigation);
        pTabs->setContentsMargins(5, 5, 5, 5);
        pTabs->setSpacing(2);
        m_pMobileNavigation->setAttribute(Qt::WA_StyledBackground);
        const QStringList vShortNames{"书架", "最近更新", "下载缓存", "我的"};
        auto* pDestinations = new QButtonGroup(m_pMobileNavigation);
        for (int nI = 0; nI < 4; ++nI)
        {
            auto pTab = new NavigationButton(vShortNames[nI]);
            pTab->setObjectName("mobileNav");
            pTab->setCheckable(true);
            pTab->setAccessibleName(vShortNames[nI]);
            pTab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            pDestinations->addButton(pTab);
            setButtonIcon(pTab, nI == 0   ? Icon::Library
                                : nI == 1 ? Icon::Discover
                                : nI == 2 ? Icon::Download
                                          : Icon::Profile);
            pTabs->addWidget(pTab, 1);
            m_vMobileNav.append(pTab);
            connect(pTab, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        if (nI == 3)
                        {
                            if (!m_pPages->currentWidget()->property("rootTab").toBool())
                            {
                                showAccountPage();
                            }
                        }
                        else
                        {
                            navigate(nI);
                        }
                    });
        }
        pBody->addWidget(m_pMobileNavigation);
        m_pMobileNavigation->hide();
        m_pCards->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_pToc->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_pCardsTouch = new TouchScroll(m_pCards,
                                        [this](QPoint oPoint)
                                        {
                                            if (auto pItem = m_pCards->itemAt(oPoint))
                                            {
                                                m_pCards->setCurrentItem(pItem);
                                                m_pCards->itemClicked(pItem);
                                            }
                                        });
        m_pTocTouch = new TouchScroll(m_pToc,
                                      [this](QPoint oPoint)
                                      {
                                          if (auto pItem = m_pToc->itemAt(oPoint))
                                          {
                                              m_pToc->setCurrentItem(pItem);
                                              m_pToc->itemClicked(pItem, 0);
                                          }
                                      });
        m_pCardsRefresh = new PullRefreshIndicator(m_pCards->viewport());
        m_pTocRefresh = new PullRefreshIndicator(m_pToc->viewport());
        m_pCards->viewport()->installEventFilter(this);
        m_pToc->viewport()->installEventFilter(this);
        m_pCardsTouch->setPullToRefresh(
            [this]
            {
                return mobileMode() && !m_bLoadingBusy &&
                       m_pPages->currentIndex() == 1 &&
                       !m_pCardsRefresh->property("refreshing").toBool();
            },
            [this](qreal fProgress)
            {
                positionPullIndicators();
                m_pCardsRefresh->setProgress(fProgress);
            },
            [this]
            {
                refreshCurrentPage();
            });
        m_pTocTouch->setPullToRefresh(
            [this]
            {
                return mobileMode() && m_bAuthenticated && !m_bLoadingBusy &&
                       m_pPages->currentIndex() == 2 && (!m_bLocalBook) &&
                       !m_pTocRefresh->property("refreshing").toBool();
            },
            [this](qreal fProgress)
            {
                positionPullIndicators();
                m_pTocRefresh->setProgress(fProgress);
            },
            [this]
            {
                refreshCurrentPage();
            });
        m_pReaderTouch = new TouchScroll(
            m_pReader,
            [this](QPoint oPoint)
            {
                if (!canReadBook() || m_bLoadingBusy || m_bRestoring ||
                    m_pPages->currentIndex() != 3)
                {
                    return;
                }
                const int nWidth = m_pReader->viewport()->width();
                const bool bMiddle = oPoint.x() >= nWidth * 0.3 && oPoint.x() <= nWidth * 0.7;
                if (bMiddle)
                {
                    m_bReaderChrome = !m_bReaderChrome;
                    adaptLayout();
                }
                else if (m_pReader->readingMode() == ReadingMode::Paged)
                {
                    turnReaderPage(oPoint.x() < nWidth * 0.3 ? -1 : 1);
                }
            });
        m_pReaderTouch->setPressAction(
            [this]
            {
                m_pReader->stopMotion();
            });
        m_pReaderTouch->setDragAction([this]
        {
            if (m_pReadAloud)
            {
                m_pReadAloud->setFollow(false);
            }
        });
        m_pLoading = new LoadingPanel(m_pRoot);
        m_pLoading->setCancelable(true);
        m_pLoading->hide();
        m_pLoading->m_fnOnCancel = [this]
        {
#ifdef WENKU8_CLIENT_ONLINE
            if (m_pSession && m_bLoadingBusy)
            {
                m_pSession->cancel();
                m_pLoading->setCancelling(true);
                message("正在取消，请稍候…");
            }
#endif
        };
    }
    void ClientWindow::setTheme(Theme eTheme)
    {
        if (m_bThemeApplied && m_eTheme == eTheme)
        {
            return;
        }
        m_bThemeApplied = true;
        m_eTheme = eTheme;
        m_pLoading->setTheme(eTheme);
        m_pIllustrations->setTheme(eTheme);
        m_pCardsRefresh->setTheme(eTheme);
        m_pTocRefresh->setTheme(eTheme);
        m_oSettings.setValue("theme", int(eTheme));
        m_pThemes->blockSignals(true);
        m_pThemes->setCurrentIndex(int(eTheme));
        m_pThemes->blockSignals(false);
        applyTheme(this, eTheme);
        applyDesktopLibraryStyle();
        applyMobileLibraryStyle();
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession)
            m_pSession->setTheme(eTheme);
#endif
        m_pReader->appearance(m_nFontSize, eTheme);
        auto pPaper = findChild<QFrame*>("readingPaper");
        pPaper->setStyleSheet("QFrame#readingPaper {background:" + colors(eTheme).m_oPaper.name() +
                              ";border-radius:14px;}");
        static_cast<BookCards*>(m_pCards->itemDelegate())->m_eTheme = eTheme;
        static_cast<ChapterRows*>(m_pToc->itemDelegate())->m_eTheme = eTheme;
        m_pToc->viewport()->update();
        m_pCards->viewport()->update();
        if (m_pBodyLayout)
            adaptLayout();
    }
    void ClientWindow::navigate(int nSection)
    {
        if (m_bLoadingBusy)
            return;
        m_bReturnAfterLogin = false;
        m_nStartupNovel = 0;
        saveSpeechPosition();
        cancelSpeechChapter();
        if (m_pReadAloud)
        {
            m_pReadAloud->pause("已离开阅读页面，听书已暂停");
        }
        ++m_nBusinessContext;
        m_oLibraryReturn = {};
        if (m_nSection != nSection || !m_strActiveSearch.isEmpty())
        {
            resetDiscoveryFeed();
            m_pCards->clear();
        }
        m_strActiveSearch.clear();
        updateNetworkActions();
        clearMobilePages();
        leaveReaderFullscreen();
        saveProgress();
        ++m_nRestoreGeneration;
        m_bRestoring = false;
        m_nSection = nSection;
        m_pSearch->clear();
        for (int nI = 0; nI < m_vNav.size(); ++nI)
        {
            m_vNav[nI]->setChecked(nI == nSection);
            m_vMobileNav[nI]->setChecked(nI == nSection);
        }
        setLibraryHeading();
        enterLibrarySection();
    }
    void ClientWindow::positionPullIndicators()
    {
        for (auto pIndicator : {m_pCardsRefresh, m_pTocRefresh})
        {
            if (!pIndicator || !pIndicator->parentWidget())
                continue;
            const auto pViewport = pIndicator->parentWidget();
            const int nWidth = qMin(220, qMax(0, pViewport->width() - 16));
            pIndicator->setGeometry((pViewport->width() - nWidth) / 2, 8, nWidth, 40);
        }
    }
    bool ClientWindow::eventFilter(QObject* pWatched, QEvent* pEvent)
    {
        if (pEvent->type() == QEvent::KeyPress &&
            handleDesktopLibraryKey(pWatched, static_cast<QKeyEvent*>(pEvent)))
        {
            return true;
        }
        if (pEvent->type() == QEvent::Resize && ((m_pCards && pWatched == m_pCards->viewport()) ||
                                                 (m_pToc && pWatched == m_pToc->viewport())))
            positionPullIndicators();
        return QMainWindow::eventFilter(pWatched, pEvent);
    }
    void ClientWindow::refreshCurrentPage()
    {
        if (m_bLoadingBusy || m_pPages->currentIndex() == 0)
            return;
        m_pCovers->retryFailures();
        auto pIndicator = m_pPages->currentIndex() == 2 ? m_pTocRefresh : m_pCardsRefresh;
        if (mobileMode())
        {
            positionPullIndicators();
            pIndicator->setRefreshing(true);
        }
        if (m_pPages->currentIndex() == 2)
        {
#ifdef WENKU8_CLIENT_ONLINE
            if (m_bAuthenticated && m_pSession && m_oBook.m_nId > 0 && !m_bLocalBook)
            {
                ++m_nBusinessContext;
                updateNetworkActions();
                m_pSession->openBook(m_oBook.m_nId, true);
                return;
            }
#endif
        }
        refresh(true);
        if (!m_bLoadingBusy)
            QTimer::singleShot(160, this,
                               [pIndicator]
                               {
                                   pIndicator->setRefreshing(false);
                               });
    }
    void ClientWindow::refresh(bool bForce)
    {
        if (m_bLoadingBusy)
            return;
        updateMobileLibraryLayout();
        updateMobileContinueReading();
        if (m_nSection == 3 && m_strActiveSearch.isEmpty())
        {
            historyBooks();
            return;
        }

        if (m_nSection == 2 && m_strActiveSearch.isEmpty())
        {
            downloadedBooks();
            return;
        }
        if (!m_bAuthenticated)
        {
            return;
        }
        ++m_nBusinessContext;
        m_bDiscoveryAppendFailed = false;
        updateNetworkActions();
        m_pCount->setText(m_pCards->count() > 0 ? "正在更新，保留上次列表…" : "正在加载小说…");
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession)
        {
            if (!m_strActiveSearch.isEmpty())
                m_pSession->search(m_strActiveSearch, bForce);
            else if (m_nSection == 0)
                m_pSession->shelf(bForce);
            else
                m_pSession->latest(1, bForce);
        }
#endif
    }
    void ClientWindow::historyBooks()
    {
        std::vector<Novel> vNovels;
        for (const auto& oEntry : m_pReadingState->history())
        {
            Novel oN;
            oN.m_nId = oEntry.m_nNovel;
            oN.m_strTitle = oEntry.m_strTitle.toUtf8().toStdString();
            oN.m_strAuthor = oEntry.m_strAuthor.toUtf8().toStdString();
            vNovels.push_back(oN);
        }
        populate(vNovels);
    }
    void ClientWindow::requestCover(int nId)
    {
        if (!m_pCovers->image(nId).isNull())
            return;
        QByteArray baBytes;
        for (const auto& strUrl :
             {coverUrl(nId),
              QString("https://img.wenku8.com/image/%1/%2/%2s.jpg").arg(nId / 1000).arg(nId)})
        {
            try
            {
                auto strLocal = m_pStore->image(nId, strUrl.toStdString());
                baBytes = QByteArray(strLocal.data(), int(strLocal.size()));
                if (!baBytes.isEmpty())
                    break;
            }
            catch (const std::exception&)
            {
            }
        }
        m_pCovers->request(nId, baBytes, m_bAuthenticated);
    }
    void ClientWindow::downloadedBooks()
    {
        std::vector<Novel> vResult;
        const QDir oBooks(QString::fromStdString(m_pStore->bookDirectory(1).parent_path().u8string()));
        for (const auto& strName : oBooks.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            bool bOk = false;
            const int nId = strName.toInt(&bOk);
            if (!bOk || nId <= 0 || strName != QString::number(nId))
            {
                continue;
            }
            try
            {
                vResult.push_back(m_pStore->novel(nId));
            }
            catch (const std::exception&)
            {
            }
        }
        populate(vResult);
    }
    void ClientWindow::openBook(int nId, bool bResume)
    {
        if (m_bLoadingBusy)
            return;
        if (desktopLibraryActive() || isDiscoveryFeed())
        {
            m_oLibraryReturn = {nId, m_pCards->verticalScrollBar()->value(),
                                m_pTitle->text(), m_pSubtitle->text()};
        }
        saveSpeechPosition();
        cancelSpeechChapter();
        if (m_pReadAloud)
        {
            m_pReadAloud->stop();
        }
        ++m_nBusinessContext;
        updateNetworkActions();
        if (mobileMode())
            QGuiApplication::inputMethod()->hide();
        leaveReaderFullscreen();
        saveProgress();
        ++m_nRestoreGeneration;
        m_bRestoring = false;
        m_bResumeOnOpen = bResume;
        try
        {
            showBook(m_pStore->novel(nId), m_pStore->index(nId), true);
            return;
        }
        catch (const std::exception&)
        {
        }
#ifdef WENKU8_CLIENT_ONLINE
        if (m_bAuthenticated && m_pSession)
        {
            m_pSession->openBook(nId);
            return;
        }
#endif
        m_bResumeOnOpen = false;
        const QString strHint = "这本小说尚无可读缓存。可在「我的」登录后在线阅读。";
        message(strHint);
        m_pToast->showText(strHint, m_eTheme);
    }
    void ClientWindow::showBook(const Novel& oNovel, const std::vector<Volume>& vVolumes,
                                bool bLocal)
    {
        if (!bLocal && !m_bAuthenticated)
            return;
        m_pOperations->hide();
        m_pRefresh->hide();
        m_pTitle->setText("小说详情");
        m_pSubtitle->setText("选择章节开始，或从上次的位置继续。");
        if (m_oBook.m_nId != oNovel.m_nId)
        {
            m_pDetailFilterToggle->setChecked(false);
            m_pDetailFilter->clear();
        }
        m_oBook = oNovel;
        m_bLocalBook = bLocal;
        m_vFlat.clear();
        m_nChapter = -1;
        m_pToc->clear();
        m_bDetailFiltering = false;
        for (const auto& oVolume : vVolumes)
        {
            auto pParent = new QTreeWidgetItem(m_pToc, {text(oVolume.m_strTitle), ""});
            pParent->setFlags(Qt::ItemIsEnabled);
            for (const auto& oChapter : oVolume.m_vChapters)
            {
                auto pItem = new QTreeWidgetItem(pParent, {text(oChapter.m_strTitle), "阅读  →"});
                pItem->setData(0, Qt::UserRole, m_vFlat.size());
                m_vFlat.append(oChapter);
            }
            pParent->setText(1, QString("%1 章 · 点击收起").arg(pParent->childCount()));
        }
        m_pToc->expandAll();
        const QString strTitle = text(m_oBook.m_strTitle);
        m_pDetailTitle->setText(strTitle.simplified());
        m_pDetailTitle->setToolTip(strTitle);
        QStringList vMetadata;
        const QString strAuthor = text(m_oBook.m_strAuthor).trimmed();
        const QString strState = text(m_oBook.m_strStatus).trimmed();
        if (!strAuthor.isEmpty())
        {
            vMetadata.append(strAuthor);
        }
        if (!strState.isEmpty())
        {
            vMetadata.append(strState);
        }
        if (bLocal)
        {
            vMetadata.append("已缓存");
        }
        m_pDetailMeta->setText(vMetadata.join(" · "));
        m_pDetailMeta->setToolTip(m_pDetailMeta->text());
        m_pDetailMeta->setVisible(!vMetadata.isEmpty());
        const QString strIntro = text(m_oBook.m_strIntro).simplified();
        m_pDetailIntro->setText(strIntro.size() > 144 ? strIntro.left(144) + "…" : strIntro);
        findChild<QPushButton*>("showSynopsis")->setEnabled(!strIntro.isEmpty());
        updateBookDetailFilter();
        updateBookDetailProgress();
        m_pDetailFilterToggle->setEnabled(!m_vFlat.isEmpty() || m_pDetailFilterToggle->isChecked());
        m_pRead->setEnabled(!m_vFlat.isEmpty());
        m_pIllustrations->setVisible(!bLocal);
        auto oImage = m_pCovers->image(m_oBook.m_nId);
        m_pDetailCover->clear();
        if (!oImage.isNull())
            m_pDetailCover->setPixmap(coverPixmap(oImage, m_pDetailCover->size() - QSize(4, 6),
                                                  m_pDetailCover->devicePixelRatioF()));
        else
            m_pDetailCover->setText(m_pCovers->placeholder(m_oBook.m_nId));
        requestCover(m_oBook.m_nId);
        updateBookActions();
        highlightChapter();
        m_pPages->setCurrentIndex(2);
        message(bLocal ? "已载入本地小说。" : "选择章节开始阅读，或下载后随时继续。");
        if (m_bResumeOnOpen)
        {
            m_bResumeOnOpen = false;
            m_pRead->click();
        }
    }
    void ClientWindow::highlightChapter()
    {
        const auto oSaved = m_pReadingState->position(m_oBook.m_nId);
        QTreeWidgetItem* pTarget = nullptr;
        for (int nI = 0; nI < m_pToc->topLevelItemCount(); ++nI)
        {
            auto pParent = m_pToc->topLevelItem(nI);
            for (int nJ = 0; nJ < pParent->childCount(); ++nJ)
            {
                auto pItem = pParent->child(nJ);
                int nIndex = pItem->data(0, Qt::UserRole).toInt();
                bool bCurrent = m_nChapter == nIndex;
                bool bPrevious = oSaved && oSaved->m_nChapter == m_vFlat[nIndex].m_nId;
                pItem->setText(1, bCurrent    ? "正在阅读"
                                  : bPrevious ? "上次读到 · 继续 →"
                                              : "阅读 →");
                pItem->setData(0, Qt::UserRole + 1, bCurrent || bPrevious);
                if (bCurrent || (bPrevious && !pTarget))
                    pTarget = pItem;
            }
        }
        if (pTarget && !pTarget->isHidden() && !pTarget->parent()->isHidden())
        {
            m_pToc->setCurrentItem(pTarget);
            m_pToc->scrollToItem(pTarget);
        }
    }
    void ClientWindow::openChapter(int nIndex)
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (!m_bLocalBook && m_pSession && m_pSession->busy())
            return;
#endif
        if (!canReadBook() || nIndex < 0 || nIndex >= m_vFlat.size())
            return;
        if (m_pReadAloud && !m_bSpeechChangingChapter)
        {
            saveSpeechPosition();
            cancelSpeechChapter();
            m_pReadAloud->pause("章节已切换，可从当前章节开始听书");
        }
        ++m_nBusinessContext;
        updateNetworkActions();
        saveProgress();
        m_nPendingChapter = nIndex;
        if (m_bLocalBook)
        {
            try
            {
                renderChapter(text(m_pStore->chapter(m_oBook.m_nId, m_vFlat[nIndex].m_nId)));
            }
            catch (const std::exception&)
            {
                message("本地章节无法读取，请重新下载这本小说。");
                if (m_nSpeechPendingBook != 0)
                {
                    cancelSpeechChapter();
                    m_pReadAloud->pause("缓存章节无法读取，听书已暂停");
                }
            }
            return;
        }
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession)
            m_pSession->openChapter(m_oBook.m_nId, m_vFlat[nIndex].m_nId);
#endif
    }
    void ClientWindow::renderChapter(const QString& strContent)
    {
        if (!canReadBook() || m_nPendingChapter < 0 || m_nPendingChapter >= m_vFlat.size())
            return;
        const bool bSpeechResume = m_pReadAloud &&
            m_pReadAloud->state() == ReadAloudController::State::WaitingChapter &&
            m_nSpeechPendingBook == m_oBook.m_nId && m_nSpeechPendingChapter == m_nPendingChapter;
        const bool bSpeechPage = bSpeechResume && m_bReopenReadAloud;
        cancelSpeechChapter();
        m_bRestoring = true;
        const auto nGeneration = ++m_nRestoreGeneration;
        if (m_nChapter < 0)
        {
            m_bReaderChrome = true;
        }
        if (mobileMode())
            QGuiApplication::inputMethod()->hide();
        m_pTitle->setText("阅读时间");
        m_pSubtitle->setText(text(m_oBook.m_strTitle));
        m_pOperations->hide();
        m_pRefresh->hide();
        m_nChapter = m_nPendingChapter;
        m_pPages->setCurrentIndex(3);
        m_pReader->showChapter(text(m_vFlat[m_nChapter].m_strTitle), strContent,
                               [this](const QString& strUrl)
                               {
                                   try
                                   {
                                       auto strBytes = m_pStore->image(
                                           m_oBook.m_nId, strUrl.toUtf8().toStdString());
                                       return QByteArray(strBytes.data(), int(strBytes.size()));
                                   }
                                   catch (const std::exception&)
                                   {
#ifdef WENKU8_CLIENT_ONLINE
                                       if (m_pSession && !m_bLocalBook)
                                           return m_pSession->imageBytes(strUrl);
#endif
                                       return QByteArray();
                                   }
                               });
        m_pReadTitle->setText(text(m_vFlat[m_nChapter].m_strTitle));
        m_pReadBook->setText(text(m_oBook.m_strTitle));
        m_pReadBook->setToolTip(m_pReadBook->text());
        m_pReadTitle->setToolTip(m_pReadTitle->text());
        updateReaderFooter();
        auto oSaved = m_pReadingState->position(m_oBook.m_nId);
        int nOffset = m_nRestoreOffset >= 0 ? m_nRestoreOffset
                      : oSaved && oSaved->m_nChapter == m_vFlat[m_nChapter].m_nId
                          ? oSaved->m_nOffset
                          : 0;
        m_nRestoreOffset = -1;
        m_pReader->restorePosition(nOffset);
        QTimer::singleShot(0, this,
                           [this, nGeneration, nOffset, bSpeechResume, bSpeechPage]
                           {
                               if (nGeneration != m_nRestoreGeneration || !m_pReadAloud || !canReadBook() ||
                                   m_pPages->currentIndex() != 3)
                                   return;
                               m_pReader->restorePosition(nOffset);
                               m_bRestoring = false;
                               saveProgress(true);
                               highlightChapter();
                               const bool bStillWaiting = bSpeechResume &&
                                   m_pReadAloud->state() == ReadAloudController::State::WaitingChapter;
                               m_pReadAloud->loadChapter(m_oBook.m_nId,
                                   m_vFlat[m_nChapter].m_nId, m_pReader->document()->toPlainText(),
                                   nOffset, bStillWaiting);
                               if (bSpeechPage && bStillWaiting &&
                                   QGuiApplication::applicationState() == Qt::ApplicationActive)
                               {
                                   showReadAloud();
                               }
                           });
        message(m_bLocalBook ? "缓存阅读 · 阅读位置自动保存" : "在线阅读 · 阅读位置自动保存");
        if (!mobileMode() && m_pReaderDock && m_bReaderDockRequested)
        {
            const QString strPanel = m_pReaderDock->property("panelTitle").toString();
            QTimer::singleShot(0, this, [this, strPanel, nGeneration]
            {
                if (nGeneration != m_nRestoreGeneration || m_pPages->currentIndex() != 3 ||
                    !m_bReaderDockRequested || mobileMode())
                {
                    return;
                }
                if (strPanel == "目录")
                {
                    showReaderContents();
                }
                else if (strPanel == "章内查找")
                {
                    showReaderSearch();
                }
                else if (strPanel == "听书")
                {
                    showReadAloud();
                }
                else
                {
                    showDesktopBookmarks();
                }
            });
        }
    }
    QString ClientWindow::progressKey() const
    {
        return "reading/" + m_strOrigin + "/" + QString::number(m_oBook.m_nId);
    }
    void ClientWindow::saveProgress(bool bVisited)
    {
        if (!canReadBook() || m_bRestoring || m_pPages->currentIndex() != 3 || m_nChapter < 0 ||
            m_nChapter >= m_vFlat.size())
            return;
        ReadingEntry oE;
        oE.m_nNovel = m_oBook.m_nId;
        oE.m_strTitle = text(m_oBook.m_strTitle);
        oE.m_strAuthor = text(m_oBook.m_strAuthor);
        oE.m_nChapter = m_vFlat[m_nChapter].m_nId;
        oE.m_strChapterTitle = text(m_vFlat[m_nChapter].m_strTitle);
        oE.m_nOffset = m_pReader->readingPosition();
        oE.m_strSnippet = m_pReader->readingSnippet(oE.m_nOffset);
        oE.m_fProgress =
            (m_nChapter +
             double(oE.m_nOffset) / std::max(1, m_pReader->document()->characterCount() - 1)) /
            std::max(1, m_vFlat.size());
        m_pReadingState->save(oE, bVisited);
    }
    void ClientWindow::addBookmark()
    {
        if (!canReadBook() || m_bRestoring || m_nChapter < 0 || m_pPages->currentIndex() != 3)
            return;

        BookmarkEntry oEntry;
        oEntry.m_nChapter = m_vFlat[m_nChapter].m_nId;
        oEntry.m_nOffset = m_pReader->readingPosition();
        oEntry.m_strTitle = text(m_vFlat[m_nChapter].m_strTitle);
        oEntry.m_strSnippet = m_pReader->readingSnippet(oEntry.m_nOffset);
        m_pReadingState->addBookmark(m_oBook.m_nId, oEntry);
        message("书签已保存，可在「书签列表」管理和跳转。");
        if (!mobileMode() && m_pReaderDock && m_pReaderDock->isVisible() &&
            m_pReaderDock->property("panelTitle").toString() == "书签")
        {
            const int nBook = m_oBook.m_nId;
            QTimer::singleShot(0, this, [this, nBook]
            {
                if (m_oBook.m_nId == nBook && m_pReaderDock && m_pReaderDock->isVisible() &&
                    m_pReaderDock->property("panelTitle").toString() == "书签")
                {
                    showDesktopBookmarks();
                }
            });
        }
    }
    void ClientWindow::showBookmarks()
    {
        if (!canReadBook() || m_bLoadingBusy)
        {
            return;
        }
        if (mobileMode())
        {
            showMobileBookmarks();
        }
        else
        {
            showDesktopBookmarks();
        }
    }
    void ClientWindow::updateBookActions()
    {
        const bool bSaved = m_vShelfIds.contains(m_oBook.m_nId);
        m_pShelfAdd->setText(bSaved ? "已在书架" : "加入书架");
        m_pShelfAdd->setToolTip(bSaved ? "可以在「我的书架」中找到这本小说"
                                       : "添加到文库网站的个人书架");
        const bool bOnlineActions = m_bAuthenticated && m_oBook.m_nId > 0 && !m_bLoadingBusy;
        m_pShelfAdd->setEnabled(bOnlineActions && !bSaved);
        m_pDownload->setEnabled(bOnlineActions && !m_bLocalBook);
        m_pLocalDownload->setEnabled(bOnlineActions);
    }
    void ClientWindow::toggleReaderFullscreen()
    {
        if (m_bReaderFullscreen)
        {
            leaveReaderFullscreen();
            return;
        }
        if (m_pPages->currentIndex() != 3)
            return;
        saveProgress();
        m_oPreviousWindowState = windowState();
        m_bReaderFullscreen = true;
        m_pSidebar->hide();
        m_pHeading->hide();
        m_pOperations->hide();
        m_pFullscreen->setText("退出全屏");
        showFullScreen();
        adaptLayout();
    }
    void ClientWindow::leaveReaderFullscreen()
    {
        if (!m_bReaderFullscreen)
            return;
        saveProgress();
        m_bReaderFullscreen = false;
        m_pSidebar->show();
        m_pHeading->show();
        m_pFullscreen->setText("全屏阅读");
        if (m_oPreviousWindowState & Qt::WindowMaximized)
            showMaximized();
        else
            showNormal();
        adaptLayout();
    }
    void ClientWindow::updateBusy(bool bBusy)
    {
        const bool bWasBusy = m_bLoadingBusy;
        const bool bAppending = m_nDiscoveryPendingPage != 0;
        m_bLoadingBusy = bBusy;
        for (auto pShortcut : findChildren<QShortcut*>())
            pShortcut->setEnabled(!bBusy);
        if (m_pDesktopMenuBar)
            m_pDesktopMenuBar->setEnabled(!bBusy);
        auto pShell = findChild<QWidget*>("shell");
        if (bBusy && !bAppending)
        {
            if (!bWasBusy)
                m_pLoadingFocus = QApplication::focusWidget();
            m_pReader->stopMotion();
            QScroller::scroller(m_pCards->viewport())->stop();
            QScroller::scroller(m_pToc->viewport())->stop();
            m_pLoading->setMessage("正在加载…");
            m_pLoading->setProgress(0, 0);
            m_pLoading->setCancelling(false);
            pShell->setEnabled(false);
            m_pLoading->setGeometry(m_pRoot->rect());
            m_pLoading->show();
            m_pLoading->raise();
            m_pLoading->setFocus(Qt::OtherFocusReason);
        }
        else if (!bBusy)
        {
            pShell->setEnabled(true);
            m_pLoading->hide();
            m_pCardsRefresh->setRefreshing(false);
            m_pTocRefresh->setRefreshing(false);
            if (m_pLoadingFocus && m_pLoadingFocus->isVisible() && m_pLoadingFocus->isEnabled())
                m_pLoadingFocus->setFocus(Qt::OtherFocusReason);
            m_pLoadingFocus = nullptr;
        }
        updateBookActions();
        m_pOperations->setEnabled(m_bAuthenticated && !bBusy);
        m_pRefresh->setEnabled(!bBusy);
        m_pDesktopAccount->setEnabled(!bBusy);
        updateAccountControls();
        for (auto pB : m_vNav)
            pB->setEnabled(!bBusy);
        for (auto pB : m_vMobileNav)
            pB->setEnabled(!bBusy);
        m_pRead->setEnabled(!bBusy && !m_vFlat.isEmpty());
        m_pToc->setEnabled(!bBusy);
        m_pPrevious->setEnabled(
            !bBusy && (m_nChapter > 0 || (m_pReader->readingMode() == ReadingMode::Paged &&
                                          m_pReader->pageNumber() > 1)));
        m_pNext->setEnabled(!bBusy && (m_nChapter + 1 < m_vFlat.size() ||
                                       (m_pReader->readingMode() == ReadingMode::Paged &&
                                        m_pReader->pageNumber() < m_pReader->pageCount())));
        m_pCards->setEnabled(!bBusy || bAppending);
        updateDesktopLibraryLayout();
        updateReaderProgress();
        updateVolumePaging();
        updateNetworkActions();
        if (!bBusy && m_pPages->currentIndex() == 1 && m_pCount->text().contains("正在"))
            m_pCount->setText(m_pCards->count()
                                  ? QString("%1 本小说 · 已保留上次列表").arg(m_pCards->count())
                                  : "暂未获取到小说，可刷新重试。");
    }
    void ClientWindow::message(const QString& strText)
    {
        m_pStatus->setText(strText);
        if (m_bLoadingBusy)
            m_pLoading->setMessage(strText);
    }
    void ClientWindow::updateNetworkActions()
    {
        if (!m_pStatusArea)
            return;
        bool bRetry = false;
#ifdef WENKU8_CLIENT_ONLINE
        bRetry = m_pSession && m_pSession->hasRetryableRequest();
#endif
        const bool bRecoverable =
            m_bAuthenticated && !m_bLoadingBusy && m_nRetryContext == m_nBusinessContext;
        const bool bRetryAppend = !m_bLoadingBusy && isDiscoveryFeed() && m_bDiscoveryAppendFailed;
        const bool bRetryVisible = (bRecoverable && bRetry) || bRetryAppend;
        m_pRetryNetwork->setText(bRetryAppend ? "重试加载更多" : "重新连接");
        m_pRetryNetwork->setVisible(bRetryVisible);
        const bool bAction = bRetryVisible;
        const bool bNormal = !mobileMode() || (m_pPages->currentIndex() != 3 &&
                                               m_pPages->currentIndex() < 4 && height() >= 520);
        m_pStatus->setVisible(bNormal || bAction);
        m_pStatusArea->setVisible(bNormal || bAction);
    }

    void ClientWindow::adaptLayout()
    {
        if (m_bAdaptingLayout)
        {
            return;
        }
        QScopedValueRollback<bool> oLayoutGuard(m_bAdaptingLayout, true);
        if (m_pDownloadTasks)
        {
            m_pDownloadTasks->setVisible(m_bAuthenticated && m_nSection == 2 &&
                m_pPages->currentIndex() == 1 && m_strActiveSearch.isEmpty());
        }
        if (!m_pReader || !m_pMobileNavigation)
            return;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        const int nBreakpoint = 1000;
#else
        const int nBreakpoint = 760;
#endif
        m_bCompact = width() < nBreakpoint;
        if (property("compact").toBool() != m_bCompact)
        {
            setProperty("compact", m_bCompact);
        }
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        if (windowHandle() && screen())
        {
            const QRect oGlobal(mapToGlobal(QPoint(0, 0)), size());
            const QRect oSafe = oGlobal.intersected(screen()->availableGeometry());
            const auto oAndroidInsets = getAndroidSafeInsets();
            const qreal fDpr = qMax(qreal(1), devicePixelRatioF());
            const QMargins oMargins(
                qMax(oSafe.isEmpty() ? 0 : qMax(0, oSafe.left() - oGlobal.left()), qCeil(oAndroidInsets.left() / fDpr)),
                qMax(oSafe.isEmpty() ? 0 : qMax(0, oSafe.top() - oGlobal.top()), qCeil(oAndroidInsets.top() / fDpr)),
                qMax(oSafe.isEmpty() ? 0 : qMax(0, oGlobal.right() - oSafe.right()), qCeil(oAndroidInsets.right() / fDpr)),
                qMax(oSafe.isEmpty() ? 0 : qMax(0, oGlobal.bottom() - oSafe.bottom()), qCeil(oAndroidInsets.bottom() / fDpr)));
            if (centralWidget()->layout()->contentsMargins() != oMargins)
            {
                centralWidget()->layout()->setContentsMargins(oMargins);
            }
        }
#endif
        const bool bMobile = mobileMode(), bReading = m_pPages->currentIndex() == 3,
                   bExtra = m_pPages->currentIndex() >= 4;
        if (m_pDesktopMenuBar)
            m_pDesktopMenuBar->setVisible(!bMobile && !m_bReaderFullscreen);
        if (m_pReaderDock)
        {
            if (m_pReaderDock->property("bookId").toInt() != m_oBook.m_nId)
            {
                m_bReaderDockRequested = false;
            }
            m_pReaderDock->setVisible(!bMobile && bReading && m_bReaderDockRequested);
        }
        const bool bTab = bExtra && m_pPages->currentWidget()->property("rootTab").toBool();
        const bool bAtLibrary = m_pPages->currentIndex() <= 1;
        const bool bLandscape = bMobile && height() < 520;
        const bool bMobileSearch = bMobile && m_pPages->currentIndex() == 1 &&
                                   !m_strActiveSearch.isEmpty();
        if (property("touchUi").toBool() != bMobile)
        {
            setProperty("touchUi", bMobile);
            applyTheme(this, m_eTheme);
            applyDesktopLibraryStyle();
            applyMobileLibraryStyle();
            updateMobileContinueReading();
        }
        m_pIllustrations->setTouchUi(bMobile);
        updateMobileLibraryLayout();
        updateDesktopLibraryLayout();
        m_pPages->setMotionEnabled(bMobile);
        m_pCardsTouch->setEnabled(bMobile);
        m_pTocTouch->setEnabled(bMobile);
        m_pReaderTouch->setEnabled(bMobile);
        m_pReader->setTextInteractionFlags(bMobile ? Qt::NoTextInteraction
                                                   : Qt::TextSelectableByMouse |
                                                         Qt::TextSelectableByKeyboard);
        m_pSidebar->setFixedWidth(width() < 1000 ? 160 : 216);
        m_pSidebar->setVisible(!bMobile && !m_bReaderFullscreen);
        m_pMobileHeader->setVisible(bMobile && !bReading);
        m_pMobileNavigation->setVisible(bMobile && (bTab || (bAtLibrary && !bMobileSearch)));
        m_pMobileTitle->setText(bExtra
                                    ? m_pPages->currentWidget()->property("mobileTitle").toString()
                                : bAtLibrary ? m_pTitle->text()
                                : m_pPages->currentIndex() == 2 ? QString("小说详情")
                                                                : applicationName());
        m_pHeading->setVisible(!m_bReaderFullscreen && !bMobile && !bReading && !bTab);
        m_pSubtitle->setVisible(!bMobile);
        findChild<QPushButton*>("mobileBack")->setVisible(
            bMobile && ((m_pPages->currentIndex() >= 2 && !bTab) || bMobileSearch));
        findChild<QPushButton*>("detailBack")->setVisible(!bMobile);
        m_pBodyLayout->setContentsMargins(bMobile ? 12 : 36, (bReading || bMobile) ? 8 : 30,
                                          bMobile ? 12 : 36, bMobile ? 8 : 18);
        m_pBodyLayout->setSpacing(bMobile ? 8 : 20);
        updateBookDetailLayout(bMobile, bLandscape);
        const auto oColors = colors(m_eTheme);
        const QString strTitleStyle = QString("font-size:%1px;font-weight:600;color:%2;")
            .arg(bMobile ? 22 : 28).arg(oColors.m_oText.name());
        if (m_pTitle->styleSheet() != strTitleStyle)
        {
            m_pTitle->setStyleSheet(strTitleStyle);
        }
        m_pToc->setColumnHidden(1, bMobile);
        m_pToc->setIndentation(bMobile ? 14 : 24);
        m_pToc->setHorizontalScrollBarPolicy(bMobile ? Qt::ScrollBarAlwaysOff
                                                     : Qt::ScrollBarAsNeeded);
        m_pReaderToolsToggle->setVisible(bMobile);
        m_pReaderToolsToggle->setText("设置");
        m_pReadBook->setAlignment(bMobile ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
        findChild<QPushButton*>("readerSidebarToggle")->setVisible(!bMobile);
        m_pReadTitle->setAlignment(bMobile ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
        for (auto pAction : {findChild<QPushButton*>("readerBack"), m_pReaderToolsToggle})
        {
            pAction->setMinimumWidth(bMobile ? 80 : 0);
            pAction->setMaximumWidth(bMobile ? 80 : QWIDGETSIZE_MAX);
        }
        m_pReaderTools->setVisible(!bMobile);
        m_pFullscreen->setVisible(!bMobile);
        m_pFullscreen->setText(m_bReaderFullscreen ? "退出全屏" : "全屏阅读");
        findChild<QWidget*>("readerTop")->setVisible(!bMobile || m_bReaderChrome);
        auto* pReaderFooter = findChild<QWidget*>("readerFooter");
        pReaderFooter->setVisible(!bMobile || m_bReaderChrome);
        findChild<QWidget*>("readerQuickTools")->setVisible(bMobile);
        // In short wide windows put navigation and tools on one row. The narrow
        // layout keeps both groups and their touch targets instead of hiding tools.
        const bool bSideBySide = bLandscape && width() >= 540;
        const auto eDirection = bSideBySide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (m_pReaderActionLayout->direction() != eDirection)
        {
            m_pReaderActionLayout->setDirection(eDirection);
            pReaderFooter->updateGeometry();
        }
        for (auto* pAction : m_vReaderQuickActions)
        {
            pAction->setText(bSideBySide && width() < 680 ? QString() : pAction->accessibleName());
        }
        m_pChapterLabel->setVisible(!bMobile);
        m_pReaderProgressPercent->setMinimumWidth(bMobile ? 42 : 70);
        updateReaderPositionText(m_pReaderProgress->value());
        m_pOperations->setVisible(m_bAuthenticated && !m_bReaderFullscreen &&
                                  m_pPages->currentIndex() == 1 && (!bMobile || m_nSection < 2));
        m_pRefresh->setVisible(
            !bMobile && (m_pPages->currentIndex() == 1 ||
                         (m_pPages->currentIndex() == 2 && m_bAuthenticated && !m_bLocalBook)));
        positionPullIndicators();
        if (!bMobile || m_pPages->currentIndex() != 1)
        {
            m_pCardsRefresh->setRefreshing(false);
            m_pCardsRefresh->setProgress(0);
        }
        if (!bMobile || m_pPages->currentIndex() != 2)
        {
            m_pTocRefresh->setRefreshing(false);
            m_pTocRefresh->setProgress(0);
        }
        updateNetworkActions();
        for (int nI = 0; nI < m_vMobileNav.size(); ++nI)
            m_vMobileNav[nI]->setChecked(bTab ? nI == 3 : nI == m_nSection);
        (bTab ? m_pDesktopAccount : m_vNav[m_nSection])->setChecked(true);
        if (bTab)
        {
            auto* pPage = m_pPages->currentWidget();
            if (auto* pAccountHeading = pPage->findChild<QWidget*>("desktopAccountHeader"))
            {
                pAccountHeading->setVisible(!bMobile);
            }
        }
        updateVolumePaging();
        m_pCards->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (bMobile)
        {
            for (auto pControl : findChildren<QPushButton*>())
                pControl->setMinimumHeight(pControl->objectName() == "themeChoice" ? 80
                                           : pControl->objectName() == "mobileNav" ? 56
                                           : pControl->objectName() == "mobileContinueReading" ? 108
                                                                                              : 48);
            m_pThemes->setMinimumHeight(48);
            m_pSearch->setMinimumHeight(48);
        }
        else
        {
            for (auto pControl : findChildren<QPushButton*>())
                pControl->setMinimumHeight(0);
            m_pThemes->setMinimumHeight(42);
            m_pSearch->setMinimumHeight(0);
        }
    }
    void ClientWindow::goBack()
    {
        if (m_bLoadingBusy)
            return;
        if (!m_vMobileHistory.isEmpty())
        {
            popMobilePage();
            return;
        }
        if (m_bReaderFullscreen)
        {
            leaveReaderFullscreen();
            return;
        }
        if (m_pPages->currentIndex() == 3)
        {
            saveProgress();
            ++m_nRestoreGeneration;
            m_bRestoring = false;
            m_bReaderToolsOpen = false;
            m_pPages->setCurrentIndex(2);
            m_pTitle->setText("小说详情");
            return;
        }
        if (m_pPages->currentIndex() == 2 && returnToLibrary())
        {
            return;
        }
        if (m_pPages->currentIndex() == 2 && mobileMode() && !m_strActiveSearch.isEmpty())
        {
            m_oLibraryReturn = {};
            m_pTitle->setText("搜索结果");
            m_pSubtitle->setText(QString("关键词「%1」").arg(m_strActiveSearch));
            m_pPages->returnTo(1);
        }
        else if (m_pPages->currentIndex() == 2 ||
                 (m_pPages->currentIndex() == 1 && !m_strActiveSearch.isEmpty()))
        {
            navigate(m_nSection);
        }
    }
    void ClientWindow::keyPressEvent(QKeyEvent* pEvent)
    {
        if (m_bLoadingBusy)
        {
            if ((pEvent->key() == Qt::Key_Back || pEvent->key() == Qt::Key_Escape) &&
                m_pLoading->m_fnOnCancel)
                m_pLoading->m_fnOnCancel();
            pEvent->accept();
            return;
        }
        if (canReadBook() && m_bVolumePaging && m_pPages->currentIndex() == 3 &&
            (!m_pReadAloud || m_pReadAloud->state() != ReadAloudController::State::Playing) &&
            (pEvent->key() == Qt::Key_VolumeUp || pEvent->key() == Qt::Key_VolumeDown))
        {
            if (!pEvent->isAutoRepeat())
                turnReaderPage(pEvent->key() == Qt::Key_VolumeUp ? -1 : 1);
            pEvent->accept();
            return;
        }
        if (mobileMode() && pEvent->key() == Qt::Key_Back &&
            QGuiApplication::inputMethod()->isVisible())
        {
            QGuiApplication::inputMethod()->hide();
            pEvent->accept();
            return;
        }
#ifdef Q_OS_ANDROID
        if (pEvent->key() == Qt::Key_Back && m_pPages->currentIndex() <= 1 && !m_bReaderFullscreen &&
            m_strActiveSearch.isEmpty())
        {
            close();
            pEvent->accept();
            return;
        }
#endif
        if (pEvent->key() == Qt::Key_Back || (m_bCompact && pEvent->key() == Qt::Key_Escape))
        {
            goBack();
            pEvent->accept();
            return;
        }
        QMainWindow::keyPressEvent(pEvent);
    }
    void ClientWindow::closeEvent(QCloseEvent* pEvent)
    {
        saveProgress();
        saveSpeechPosition();
        if (m_pReadAloud)
        {
            m_pReadAloud->stop();
        }
        m_oSettings.sync();
        QMainWindow::closeEvent(pEvent);
    }
    void ClientWindow::resizeEvent(QResizeEvent* pEvent)
    {
        QMainWindow::resizeEvent(pEvent);
        if (m_pReader)
            adaptLayout();
        if (m_pLoading)
            m_pLoading->setGeometry(m_pRoot->rect());
    }
} // namespace Wenku8::Reader
