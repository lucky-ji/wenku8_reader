// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "DownloadManager.h"
#include "DownloadTaskPanel.h"
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
#include <algorithm>
#include <QShortcut>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBlock>
#include <QHeaderView>
#include <QLinearGradient>
#include <QItemSelectionModel>
#include <QScroller>
#include <QKeyEvent>
#include <QScreen>
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
                return {74, 64};
            }

          protected:
            void paintEvent(QPaintEvent*) override
            {
                QPainter oPainter(this);
                oPainter.setRenderHint(QPainter::Antialiasing);
                auto oInk = property(isChecked() ? "navActive" : "navMuted").value<QColor>();
                if (!oInk.isValid())
                    oInk = palette().color(QPalette::ButtonText);
                if (isDown())
                {
                    oPainter.setPen(Qt::NoPen);
                    oPainter.setBrush(property("navPressed").value<QColor>());
                    oPainter.drawRoundedRect(rect().adjusted(2, 1, -2, -1), 14, 14);
                }
                QRect oPill((width() - 54) / 2, 3, 54, 30);
                if (isChecked())
                {
                    oPainter.setPen(Qt::NoPen);
                    oPainter.setBrush(property("navSelection").value<QColor>());
                    oPainter.drawRoundedRect(oPill, 15, 15);
                }
                makeIcon(Icon(property("wenkuIcon").toInt()), oInk,
                         property("navDisabled").value<QColor>(), oInk)
                    .paint(&oPainter, QRect((width() - 22) / 2, 7, 22, 22), Qt::AlignCenter,
                           isEnabled() ? QIcon::Normal : QIcon::Disabled);
                auto oF = font();
                oF.setPixelSize(12);
                oF.setBold(isChecked());
                oPainter.setFont(oF);
                oPainter.setPen(isEnabled() ? oInk : property("navDisabled").value<QColor>());
                oPainter.drawText(QRect(0, 36, width(), 24), Qt::AlignCenter, text());
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
        class BookCards final : public QStyledItemDelegate
        {
          public:
            Theme m_eTheme = Theme::Light;
            using QStyledItemDelegate::QStyledItemDelegate;
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
                    int nColumns = qMax(2, nWidth / 176),
                        nCell = qMax(116, (nWidth - nColumns * 12 - 2) / nColumns);
                    return {nCell, qRound(qMin(130, nCell - 36) * 1.45) + 112};
                }
                int nColumns = qMax(1, (nWidth + 6) / 322);
                return {qMax(240, (nWidth - 12 - (nColumns - 1) * 12) / nColumns), 188};
            }
            void paint(QPainter* pP, const QStyleOptionViewItem& oO,
                       const QModelIndex& oIndex) const override
            {
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
                auto pView = qobject_cast<const QListWidget*>(parent());
                if (pView && pView->property("mobileCards").toBool())
                {
                    int nCw = qMin(130, oR.width() - 24), nCh = qRound(nCw * 1.45);
                    QRect oCover(oR.center().x() - nCw / 2, oR.top() + 12, nCw, nCh);
                    auto oImage = oIndex.data(Qt::UserRole + 3).value<QImage>();
                    QPainterPath oClip;
                    oClip.addRoundedRect(oCover, 8, 8);
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
                            .paint(pP, QRect(oCover.center().x() - 16, oCover.center().y() - 30, 32,
                                             32));
                        pP->setPen(oC.m_oMuted);
                        pP->drawText(oCover.adjusted(2, 55, -2, -4),
                                     Qt::AlignCenter | Qt::TextWordWrap,
                                     oIndex.data(Qt::UserRole + 6).toString());
                    }
                    pP->restore();
                    auto oF = oO.font;
                    oF.setPointSize(10);
                    oF.setBold(true);
                    pP->setFont(oF);
                    pP->setPen(oC.m_oText);
                    pP->drawText(QRect(oR.left() + 10, oCover.bottom() + 10, oR.width() - 20, 38),
                                 Qt::TextWordWrap, oIndex.data().toString());
                    oF.setPointSize(8);
                    oF.setBold(false);
                    pP->setFont(oF);
                    pP->setPen(oC.m_oMuted);
                    pP->drawText(
                        QRect(oR.left() + 10, oCover.bottom() + 50, oR.width() - 20, 18),
                        Qt::AlignVCenter,
                        pP->fontMetrics().elidedText(oIndex.data(Qt::UserRole + 1).toString(),
                                                     Qt::ElideRight, oR.width() - 20));
                    pP->setPen(oC.m_oAccent);
                    pP->drawText(QRect(oR.left() + 10, oCover.bottom() + 70, oR.width() - 20, 18),
                                 Qt::AlignVCenter,
                                 pP->fontMetrics().elidedText(oIndex.data(Qt::UserRole + 2).toString(),
                                                              Qt::ElideRight, oR.width() - 20));
                    pP->restore();
                    return;
                }
                QRect oCover(oR.left() + 18, oR.top() + 19, 74, 118);
                auto oImage = oIndex.data(Qt::UserRole + 3).value<QImage>();
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
                    pP->drawText(oCover.adjusted(2, 62, -2, -4), Qt::AlignCenter | Qt::TextWordWrap,
                                 oIndex.data(Qt::UserRole + 6).toString().isEmpty()
                                     ? "封面加载中"
                                     : oIndex.data(Qt::UserRole + 6).toString());
                }
                pP->restore();
                int nX = oCover.right() + 16, nW = oR.right() - nX - 14;
                auto oFont = oO.font;
                oFont.setPointSize(11);
                oFont.setBold(true);
                pP->setFont(oFont);
                pP->setPen(bEnabled ? oC.m_oText : oC.m_oDisabled);
                pP->drawText(QRect(nX, oR.top() + 20, nW, 52), Qt::TextWordWrap,
                             oIndex.data().toString());
                oFont.setPointSize(9);
                oFont.setBold(false);
                pP->setFont(oFont);
                pP->setPen(oC.m_oMuted);
                pP->drawText(QRect(nX, oR.top() + 77, nW, 20), Qt::AlignVCenter,
                             pP->fontMetrics().elidedText(oIndex.data(Qt::UserRole + 1).toString(),
                                                          Qt::ElideRight, nW));
                auto strStatus = oIndex.data(Qt::UserRole + 2).toString();
                if (!strStatus.isEmpty())
                {
                    QRect oBadge(nX, oR.top() + 106,
                                 std::min(nW, pP->fontMetrics().horizontalAdvance(strStatus) + 16), 23);
                    pP->setPen(Qt::NoPen);
                    pP->setBrush(oC.m_oSelection);
                    pP->drawRoundedRect(oBadge, 6, 6);
                    pP->setPen(oC.m_oSelectionText);
                    pP->drawText(
                        oBadge.adjusted(8, 0, -5, 0), Qt::AlignVCenter,
                        pP->fontMetrics().elidedText(strStatus, Qt::ElideRight, oBadge.width() - 12));
                }
                pP->setPen(oC.m_oMuted);
                pP->drawText(QRect(oR.left() + 18, oR.top() + 144, oR.width() - 36, 22),
                             Qt::AlignVCenter,
                             pP->fontMetrics().elidedText(oIndex.data(Qt::UserRole + 4).toString(),
                                                          Qt::ElideRight, oR.width() - 36));
                if (bHover || bSelected)
                    makeIcon(Icon::Right, oC.m_oSelectionText, oC.m_oDisabled, oC.m_oSelectionText)
                        .paint(pP, QRect(oR.right() - 28, oR.bottom() - 29, 16, 16));
                pP->restore();
            }
        };
    } // namespace
    ClientWindow::ClientWindow(const QString& strDirectory, const QString& strOrigin,
                               const QString& strSessionDirectory)
        : m_strOrigin(strOrigin)
    {
        m_strDirectory = strDirectory.isEmpty() ? privateDownloadDirectory() : strDirectory;
        m_pReadingState = std::make_unique<ReadingState>(m_oSettings, m_strOrigin);
        m_pCovers = std::make_unique<CoverStore>(
            m_strOrigin, QDir(m_strDirectory).filePath(".ui-cache"), nullptr);
        connect(m_pCovers.get(), &CoverStore::ready, this,
                [this](int nId, const QImage& oImage)
                {
                    if (!m_bUnlocked)
                        return;
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
                    if (m_oBook.m_nId == nId && m_pDetailCover)
                    {
                        if (!oImage.isNull())
                            m_pDetailCover->setPixmap(
                                coverPixmap(oImage, m_pDetailCover->size() - QSize(4, 6),
                                            m_pDetailCover->devicePixelRatioF()));
                        else
                            m_pDetailCover->setText(m_pCovers->placeholder(nId));
                    }
                });
        connect(m_pCovers.get(), &CoverStore::statusChanged, this,
                [this](int nId)
                {
                    if (!m_bUnlocked)
                        return;
                    for (int nI = 0; nI < m_pCards->count(); ++nI)
                    {
                        auto pItem = m_pCards->item(nI);
                        if (pItem->data(Qt::UserRole).toInt() == nId)
                            pItem->setData(Qt::UserRole + 6, m_pCovers->placeholder(nId));
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
        m_pReader->setTypography(m_oSettings.value("readerLineHeight", 145).toInt(),
                                m_oSettings.value("readerParagraphSpacing", 8).toInt(),
                                m_oSettings.value("readerSideMargin", 0).toInt());
        m_bVolumePaging = m_oSettings.value("volumePageKeys", true).toBool();
        setReadingMode(ReadingMode(qBound(0, m_oSettings.value("readingMode", 0).toInt(), 1)));
        setTheme(m_eTheme);
        adaptLayout();
#ifdef WENKU8_CLIENT_ONLINE
        {
            m_pStartupPages->setCurrentIndex(0);
            if (m_pDesktopMenuBar)
                m_pDesktopMenuBar->hide();
            m_pSession = new OnlineSession(this, m_strOrigin, m_strDirectory, strSessionDirectory);
            m_pSession->setTheme(m_eTheme);
            m_pToast = new Toast(m_pStartupPages);
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
                        if (m_bUnlocked)
                        {
                            requestCover(nId);
                            if (m_nSection == 2 && m_pPages->currentIndex() == 1 && !m_bLoadingBusy)
                            {
                                downloadedBooks();
                            }
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
                    [this](bool bChecking)
                    {
                        m_pStartupPages->setCurrentIndex(bChecking ? 0 : 1);
                        adaptLayout();
                    });
            connect(m_pSession, &OnlineSession::authenticationChanged, this,
                    [this](bool bValue)
                    {
                        authenticate(bValue);
                        if (bValue)
                            QTimer::singleShot(0, this,
                                               [this]
                                               {
                                                   if (!m_pSession->busy())
                                                       refresh();
                                               });
                    });
            connect(m_pSession, &OnlineSession::busyChanged, this,
                    [this](bool bValue)
                    {
                        if (bValue)
                            m_nActiveRequestContext = m_nBusinessContext;
                        updateBusy(bValue);
                    });
            connect(m_pSession, &OnlineSession::retryAvailableChanged, this,
                    [this](bool bAvailable)
                    {
                        if (bAvailable)
                            m_nRetryContext = m_nActiveRequestContext;
                        updateNetworkActions();
                        if (bAvailable && m_pPages->currentIndex() == 1 && m_pCards->count() > 0)
                            m_pCount->setText(
                                QString("暂未更新 · 保留上次列表（%1 本）").arg(m_pCards->count()));
                    });
            connect(m_pSession, &OnlineSession::chapterImagesReady, this,
                    [this](int nNovel, int nChapter)
                    {
                        if (m_bUnlocked && !m_bLocalBook && m_oBook.m_nId == nNovel &&
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
                        updateShelfAction();
                    });
            connect(m_pSession, &OnlineSession::bookshelfAdded, this,
                    [this](int nId)
                    {
                        m_vShelfIds.insert(nId);
                        updateShelfAction();
                    });
            connect(m_pSession, &OnlineSession::novelsReady, this,
                    [this]
                    {
                        populate(m_pSession->novels());
                        if (m_nStartupNovel)
                        {
                            int nId = m_nStartupNovel;
                            m_nStartupNovel = 0;
                            QTimer::singleShot(0, this,
                                               [this, nId]
                                               {
                                                   openBook(nId);
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
        authenticate(false);
        auto pScrollSave = new QTimer(this);
        pScrollSave->setSingleShot(true);
        pScrollSave->setInterval(250);
        connect(m_pReader->verticalScrollBar(), &QScrollBar::valueChanged, this,
                [this, pScrollSave]
                {
                    if (!m_bRestoring && m_pPages->currentIndex() == 3)
                        pScrollSave->start();
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
        setAndroidVolumePaging(false);
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
        m_pStartupPages = new QStackedWidget;
        m_pStartupPages->setObjectName("startupPages");
        setCentralWidget(m_pStartupPages);
        m_pStartupLoading = new LoadingPanel;
        m_pStartupLoading->setProperty("startupMode", true);
        m_pStartupLoading->setObjectName("startupLoading");
        m_pStartupLoading->setCancelable(false);
        m_pStartupLoading->setMessage("正在检查登录状态…");
        m_pStartupPages->addWidget(m_pStartupLoading);
        auto pShell = new QWidget;
        pShell->setObjectName("shell");
        m_pStartupPages->addWidget(pShell);
        m_pStartupPages->setCurrentIndex(1);
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
        pSide->addWidget(label("轻小说，随时继续。", "muted"));
        pSide->addSpacing(32);
        QStringList vNames{"我的书架", "发现小说", "下载缓存", "阅读历史"};
        for (int nI = 0; nI < vNames.size(); ++nI)
        {
            auto pB = button(vNames[nI], "nav");
            pB->setCheckable(true);
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
        const QStringList vInformationTitles{"版本说明", "更新日志", "开源许可"};
        for (int nI = 0; nI < vInformationTitles.size(); ++nI)
        {
            auto pInfo = button(vInformationTitles[nI], "quiet");
            setButtonIcon(pInfo, nI == 0 ? Icon::Profile : nI == 1 ? Icon::History : Icon::Shield);
            pSide->addWidget(pInfo);
            connect(pInfo, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        showAppInformation(nI);
                    });
        }
        pSide->addSpacing(12);
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
        m_pAccount = label("尚未登录", "muted");
        pSide->addWidget(m_pAccount);
        m_pLogin = button("登录文库", "accountButton");
        setButtonIcon(m_pLogin, Icon::SignIn);
        pSide->addWidget(m_pLogin);
        connect(m_pLogin, &QPushButton::clicked, this,
                [this]
                {
#ifdef WENKU8_CLIENT_ONLINE
                    if (m_pSession)
                        m_pSession->signIn(m_bUnlocked);
#else
        message("当前构建未启用登录组件，请使用带登录支持的客户端。");
#endif
                });
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
        auto pMobileBack = button("返回", "mobileBack");
        setButtonIcon(pMobileBack, Icon::Left);
        pMobileBack->setProperty("textAction", true);
        pMobileTop->addWidget(pMobileBack);
        connect(pMobileBack, &QPushButton::clicked, this,
                [this]
                {
                    goBack();
                });
        m_pMobileTitle = label(applicationName(), "brand");
        m_pMobileTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pMobileTop->addWidget(m_pMobileTitle, 1);
        auto pMore = button("我的", "mobileAccount");
        pMobileTop->addWidget(pMore);
        connect(pMore, &QPushButton::clicked, this,
                [this]
                {
                    showAccountSheet();
                });
        pBody->addWidget(m_pMobileHeader);
        m_pMobileHeader->hide();
        m_pHeading = new QWidget;
        auto pHeading = new QHBoxLayout(m_pHeading);
        pHeading->setContentsMargins(0, 0, 0, 0);
        auto pTitles = new QVBoxLayout;
        pTitles->setSpacing(7);
        pTitles->addWidget(label("轻小说文库", "eyebrow"));
        m_pTitle = label("欢迎来到文库", "pageTitle");
        m_pSubtitle = label("登录后，书架与下一段故事都在这里。", "subtitle");
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
        m_pSearch = new SearchInput("搜索轻小说，发现下一本喜欢的故事");
        m_pSearch->setObjectName("novelSearch");
        m_pSearch->setClearButtonEnabled(true);
        pSearchRow->addWidget(m_pSearch, 1);
        auto pSearchButton = button("搜索", "searchNovels");
        pSearchButton->setProperty("textAction", true);
        setButtonIcon(pSearchButton, Icon::Search);
        pSearchRow->addWidget(pSearchButton);
        pBody->addWidget(m_pOperations);
        auto fnDoSearch = [this]
        {
            if (!m_bUnlocked || m_bLoadingBusy || m_pSearch->text().trimmed().isEmpty())
                return;
            ++m_nBusinessContext;
            m_strActiveSearch = m_pSearch->text().trimmed();
            updateNetworkActions();
            saveProgress();
            m_pTitle->setText("搜索结果");
            m_pSubtitle->setText("关于「" + m_pSearch->text().trimmed() + "」的故事");
            m_pPages->setCurrentIndex(1);
#ifdef WENKU8_CLIENT_ONLINE
            if (m_pSession)
                m_pSession->search(m_pSearch->text().trimmed());
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
                    if (m_pLoading)
                        m_pLoading->setGeometry(m_pStartupPages->rect());
                });
        auto pLocked = new QFrame;
        pLocked->setObjectName("surface");
        auto pGate = new QVBoxLayout(pLocked);
        pGate->setContentsMargins(16, 24, 16, 24);
        pGate->addStretch();
        auto pWelcome = label("每次打开，都离故事更近一点。", "bookTitle");
        pWelcome->setWordWrap(true);
        pGate->addWidget(pWelcome);
        pGate->addSpacing(12);
        auto pWelcomeText =
            label("登录轻小说文库，查看收藏、发现新作，\n或接着上次的章节继续阅读。", "muted");
        pWelcomeText->setWordWrap(true);
        pGate->addWidget(pWelcomeText);
        pGate->addSpacing(26);
        auto pSignIn = button("登录文库", "primary");
        setButtonIcon(pSignIn, Icon::SignIn);
        pSignIn->setMaximumWidth(180);
        pGate->addWidget(pSignIn);
        connect(pSignIn, &QPushButton::clicked, m_pLogin, &QPushButton::click);
        pGate->addStretch();
        auto pPrivacy = label("账号由文库网站验证 · 密码不会保存在客户端", "muted");
        pPrivacy->setWordWrap(true);
        pGate->addWidget(pPrivacy);
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
        pLibraryBar->addWidget(m_pCount, 1);
        m_pPreviousPage = button("上一页", "quiet");
        m_pPreviousPage->setProperty("textAction", true);
        m_pNextPage = button("下一页", "quiet");
        m_pNextPage->setProperty("textAction", true);
        setButtonIcon(m_pPreviousPage, Icon::Left);
        setButtonIcon(m_pNextPage, Icon::Right);
        m_pNextPage->setLayoutDirection(Qt::RightToLeft);
        pLibraryBar->addWidget(m_pPreviousPage);
        pLibraryBar->addWidget(m_pNextPage);
        pLibraryLayout->addLayout(pLibraryBar);
        connect(m_pPreviousPage, &QPushButton::clicked, this,
                [this]
                {
                    if (m_nSection == 1 && m_nPage > 1)
                    {
                        --m_nPage;
                        refresh();
                    }
                });
        connect(m_pNextPage, &QPushButton::clicked, this,
                [this]
                {
                    if (m_nSection == 1)
                    {
                        ++m_nPage;
                        refresh();
                    }
                });
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
        m_pCards->setItemDelegate(new BookCards(m_pCards));
        pLibraryLayout->addWidget(m_pCards, 1);
        connect(m_pCards, &QListWidget::itemClicked, this,
                [this](QListWidgetItem* pItem)
                {
                    openBook(pItem->data(Qt::UserRole).toInt(), m_nSection == 3);
                });
        m_pPages->addWidget(pLibrary);
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
                    navigate(m_nSection);
                });
        auto pDetailHeader = new QHBoxLayout;
        m_pDetailHeaderLayout = pDetailHeader;
        pDetailHeader->setSpacing(26);
        m_pDetailCover = label("加载封面…", "coverImage");
        m_pDetailCover->setFixedSize(132, 192);
        m_pDetailCover->setAlignment(Qt::AlignCenter);
        m_pDetailCover->setWordWrap(true);
        pDetailHeader->addWidget(m_pDetailCover);
        auto pDetailWords = new QVBoxLayout;
        m_pDetailTitle = label("", "bookTitle");
        m_pDetailTitle->setWordWrap(true);
        m_pDetailMeta = label("", "muted");
        m_pDetailMeta->setWordWrap(true);
        m_pDetailIntro = label("", "intro");
        m_pDetailIntro->setWordWrap(true);
        m_pDetailIntro->setMaximumHeight(130);
        pDetailWords->addWidget(m_pDetailTitle);
        pDetailWords->addWidget(m_pDetailMeta);
        pDetailWords->addWidget(m_pDetailIntro);
        pDetailWords->addStretch();
        pDetailHeader->addLayout(pDetailWords, 1);
        pInfo->addLayout(pDetailHeader);
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
        auto pSynopsis = button("查看简介", "showSynopsis");
        setButtonIcon(pSynopsis, Icon::Book);
        pBookActions->addWidget(pSynopsis, 0, 2);
        for (auto pAction : {m_pDownload, m_pLocalDownload, m_pShelfAdd, pSynopsis})
        {
            pAction->setProperty("textAction", true);
        }
        connect(pSynopsis, &QPushButton::clicked, this,
                [this]
                {
                    if (mobileMode())
                    {
                        showMobileText("小说简介", m_pDetailIntro->text());
                        return;
                    }
                    QDialog oDialog(this);
                    oDialog.setObjectName("synopsisDialog");
                    oDialog.setWindowTitle("小说简介");
                    auto pLayout = new QVBoxLayout(&oDialog);
                    auto pTextView = new QTextBrowser;
                    pTextView->setPlainText(m_pDetailIntro->text());
                    pLayout->addWidget(pTextView, 1);
                    auto pClose = button("关闭");
                    pClose->setMinimumHeight(44);
                    pLayout->addWidget(pClose);
                    connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
                    applyTheme(&oDialog, m_eTheme);
                    oDialog.resize(qMin(560, width() - 24), qMin(500, height() - 40));
                    oDialog.exec();
                });
        pInfo->addLayout(pBookActions);
        pInfo->addWidget(label("章节目录", "eyebrow"));
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
        pInfo->addWidget(m_pToc, 1);
        m_pPages->addWidget(pDetail);
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
                    if (m_bUnlocked && m_pSession && m_oBook.m_nId > 0 &&
                        !m_vShelfIds.contains(m_oBook.m_nId))
                        m_pSession->addToShelf(m_oBook.m_nId);
#endif
                });
        connect(m_pDownload, &QPushButton::clicked, this, &ClientWindow::cacheBook);
        connect(m_pLocalDownload, &QPushButton::clicked, this, &ClientWindow::downloadBookToLocal);
        connect(m_pToc, &QTreeWidget::itemClicked, this,
                [this](QTreeWidgetItem* pItem, int)
                {
                    auto oValue = pItem->data(0, Qt::UserRole);
                    if (oValue.isValid())
                        openChapter(oValue.toInt());
                    else
                        pItem->setExpanded(!pItem->isExpanded());
                });
        connect(m_pToc, &QTreeWidget::itemActivated, this,
                [this](QTreeWidgetItem* pItem, int)
                {
                    auto oValue = pItem->data(0, Qt::UserRole);
                    if (oValue.isValid())
                        openChapter(oValue.toInt());
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
        m_pReadTitle = label("", "muted");
        m_pReadTitle->setAccessibleName("当前阅读章节");
        m_pReadTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pReaderTopLayout->addWidget(m_pReadTitle, 1);
        m_pFullscreen = button("全屏阅读", "readerFullscreen");
        setButtonIcon(m_pFullscreen, Icon::Fullscreen);
        m_pFullscreen->setToolTip("全屏阅读（F11），按 Esc 退出");
        m_pReaderToolsToggle = button("工具", "readerToolsToggle");
        setButtonIcon(m_pReaderToolsToggle, Icon::List);
        m_pReaderToolsToggle->setProperty("textAction", true);
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
        auto pFindText = new SearchInput("在本章查找");
        pFindText->setClearButtonEnabled(true);
        pFindText->setObjectName("chapterSearch");
        pFindText->setMinimumWidth(180);
        pReaderTools->addWidget(pFindText);
        auto pFindButton = button("查找");
        setButtonIcon(pFindButton, Icon::Search);
        pReaderTools->addWidget(pFindButton);
        auto fnFind = [this, pFindText]
        {
            if (!m_pReader->findInChapter(pFindText->text()))
                message("本章没有找到这段文字。");
        };
        connect(pFindText, &QLineEdit::returnPressed, this, fnFind);
        connect(pFindButton, &QPushButton::clicked, this, fnFind);
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
        auto pFooterActions = new QHBoxLayout;
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
            pAction->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            pFooterActions->addWidget(pAction, 1);
        }
        pFooterLayout->addLayout(pFooterActions);
        pFooterLayout->addWidget(m_pChapterLabel);
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
                    if (m_pSession && m_nRetryContext == m_nBusinessContext)
                        m_pSession->retryLastRequest();
#endif
                });
        m_pMobileNavigation = new QWidget;
        m_pMobileNavigation->setObjectName("mobileNavigation");
        auto pTabs = new QHBoxLayout(m_pMobileNavigation);
        pTabs->setContentsMargins(8, 8, 8, 6);
        pTabs->setSpacing(2);
        m_pMobileNavigation->setAttribute(Qt::WA_StyledBackground);
        const QStringList vShortNames{"书架", "发现", "下载缓存", "我的"};
        for (int nI = 0; nI < 4; ++nI)
        {
            auto pTab = new NavigationButton(vShortNames[nI]);
            pTab->setObjectName("mobileNav");
            pTab->setCheckable(true);
            pTab->setAccessibleName(vNames[nI]);
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
                            showMobileAccount(true);
                        else
                        {
                            clearMobilePages();
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
                return mobileMode() && m_bUnlocked && !m_bLoadingBusy &&
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
                return mobileMode() && m_bUnlocked && !m_bLoadingBusy &&
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
                if (m_pReader->readingMode() == ReadingMode::Paged &&
                    (oPoint.x() < m_pReader->viewport()->width() * 0.3 ||
                     oPoint.x() > m_pReader->viewport()->width() * 0.7))
                {
                    turnReaderPage(oPoint.x() < m_pReader->viewport()->width() * 0.3 ? -1 : 1);
                    return;
                }
                m_bReaderChrome = !m_bReaderChrome;
                adaptLayout();
            });
        m_pReaderTouch->setPressAction(
            [this]
            {
                m_pReader->stopMotion();
            });
        m_pLoading = new LoadingPanel(m_pStartupPages);
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
        m_pStartupLoading->setTheme(eTheme);
        m_pIllustrations->setTheme(eTheme);
        m_pCardsRefresh->setTheme(eTheme);
        m_pTocRefresh->setTheme(eTheme);
        m_oSettings.setValue("theme", int(eTheme));
        m_pThemes->blockSignals(true);
        m_pThemes->setCurrentIndex(int(eTheme));
        m_pThemes->blockSignals(false);
        applyTheme(this, eTheme);
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
    void ClientWindow::start(int nNovel)
    {
        m_nStartupNovel = nNovel;
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession)
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   if (m_pSession->persistent())
                                       m_pSession->restoreSession();
                                   else
                                   {
                                       m_pStartupPages->setCurrentIndex(1);
                                       adaptLayout();
                                       m_pSession->signIn();
                                   }
                               });
#else
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        message("移动端在线登录尚未开放，请使用桌面版。当前移动构建用于界面与阅读适配。");
#else
        message("此构建缺少登录组件。请启用登录支持后使用文库客户端。");
#endif
#endif
    }
    void ClientWindow::authenticate(bool bValue)
    {
        if (!bValue)
        {
            saveProgress();
            ++m_nBusinessContext;
        }
        m_bUnlocked = bValue;
        m_nRetryContext = m_nBusinessContext;
        updateNetworkActions();
        m_pOperations->setEnabled(bValue);
        m_pRefresh->setEnabled(bValue);
        for (auto pB : m_vNav)
            pB->setEnabled(bValue);
        for (auto pB : m_vMobileNav)
            pB->setEnabled(bValue);
        if (!bValue)
        {
            clearMobilePages();
            leaveReaderFullscreen();
            m_vShelfIds.clear();
            ++m_nRestoreGeneration;
            m_bRestoring = false;
            m_pCovers->cancelPending();
            m_pOperations->hide();
            m_pRefresh->hide();
            saveProgress();
            m_pCards->clear();
            m_vFlat.clear();
            m_oBook = {};
            m_nChapter = -1;
            m_pReader->showChapter("", "", {});
            m_pToc->clear();
            m_pPages->setCurrentIndex(0);
            m_pTitle->setText("欢迎来到文库");
            m_pSubtitle->setText("登录后，书架与下一段故事都在这里。");
            m_pAccount->setText("尚未登录");
            m_pLogin->setText("登录文库");
            m_pStore.reset();
        }
        else
        {
            // A verified session replaces the previous failure, before refresh starts.
            message("已登录，欢迎回来。");
            m_pOperations->show();
            m_pRefresh->show();
            m_pTitle->setText("我的书架");
            m_pSubtitle->setText("收藏的故事，留在触手可及的地方。");
            m_vNav[0]->setChecked(true);
            m_vMobileNav[0]->setChecked(true);
            m_pStore = openStore(m_strDirectory, m_strOrigin);
            m_pAccount->setText(
#ifdef WENKU8_CLIENT_ONLINE
                m_pSession->username()
#else
                QString()
#endif
            );
            m_pLogin->setText("切换账号");
            m_nSection = 0;
            m_pPages->setCurrentIndex(1);
            // Interactive login does not emit a second restorationChanged(false).
            // Complete the shell transition explicitly, including any stale busy
            // overlay left by the request that led to authentication.
            m_pStartupPages->setCurrentIndex(1);
#ifdef WENKU8_CLIENT_ONLINE
            updateBusy(m_pSession && m_pSession->busy());
#else
            updateBusy(false);
#endif
            adaptLayout();
        }
    }
    void ClientWindow::navigate(int nSection)
    {
        if (!m_bUnlocked || m_bLoadingBusy)
            return;
        ++m_nBusinessContext;
        m_strActiveSearch.clear();
        updateNetworkActions();
        clearMobilePages();
        leaveReaderFullscreen();
        m_pOperations->show();
        m_pRefresh->show();
        saveProgress();
        ++m_nRestoreGeneration;
        m_bRestoring = false;
        m_nSection = nSection;
        m_nPage = 1;
        m_pSearch->clear();
        for (int nI = 0; nI < m_vNav.size(); ++nI)
        {
            m_vNav[nI]->setChecked(nI == nSection);
            m_vMobileNav[nI]->setChecked(nI == nSection);
        }
        m_pTitle->setText(QStringList{"我的书架", "发现小说", "下载缓存", "阅读历史"}[nSection]);
        m_pSubtitle->setText(QStringList{"收藏的故事，留在触手可及的地方。",
                                         "看看文库最近更新了哪些故事。", "把喜欢的故事带在身边。",
                                         "每本小说，都停在你上次读到的地方。"}[nSection]);
        m_pPages->setCurrentIndex(1);
        refresh();
        adaptLayout();
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
        if (pEvent->type() == QEvent::Resize && ((m_pCards && pWatched == m_pCards->viewport()) ||
                                                 (m_pToc && pWatched == m_pToc->viewport())))
            positionPullIndicators();
        return QMainWindow::eventFilter(pWatched, pEvent);
    }
    void ClientWindow::refreshCurrentPage()
    {
        if (!m_bUnlocked || m_bLoadingBusy)
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
            if (m_pSession && m_oBook.m_nId > 0 && !m_bLocalBook)
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
        if (!m_bUnlocked || m_bLoadingBusy)
            return;
        m_pPreviousPage->setVisible(m_nSection == 1);
        m_pNextPage->setVisible(m_nSection == 1);
        m_pPreviousPage->setEnabled(m_nPage > 1);
        if (m_nSection == 3)
        {
            historyBooks();
            return;
        }

        if (m_nSection == 2)
        {
            downloadedBooks();
            return;
        }
        ++m_nBusinessContext;
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
                m_pSession->latest(m_nPage, bForce);
        }
#endif
    }
    void ClientWindow::populate(const std::vector<Novel>& vNovels)
    {
        if (!m_bUnlocked)
            return;
        m_pCards->clear();
        for (const auto& oN : vNovels)
        {
            auto pItem = new QListWidgetItem(text(oN.m_strTitle), m_pCards);
            pItem->setData(Qt::UserRole, oN.m_nId);
            pItem->setData(Qt::UserRole + 1, text(oN.m_strAuthor));
            pItem->setData(Qt::UserRole + 2,
                           m_nSection == 2 ? "已缓存" : text(oN.m_strStatus));
            pItem->setData(Qt::UserRole + 3, m_pCovers->image(oN.m_nId));
            pItem->setData(Qt::UserRole + 6, m_pCovers->placeholder(oN.m_nId));
            if (auto oSaved = m_pReadingState->position(oN.m_nId))
            {
                pItem->setData(Qt::UserRole + 4, QString("%1 · %2%")
                                                     .arg(oSaved->m_strChapterTitle.isEmpty()
                                                              ? "已读"
                                                              : oSaved->m_strChapterTitle)
                                                     .arg(int(oSaved->m_fProgress * 100)));
                if (m_nSection == 3)
                    pItem->setData(Qt::UserRole + 2,
                                   oSaved->m_oUpdated.toLocalTime().toString("MM-dd HH:mm") +
                                       " · 继续阅读");
            }
            pItem->setToolTip(text(oN.m_strTitle) + (m_nSection == 3 ? "\n点击从上次位置继续阅读"
                                                                     : "\n点击查看简介与章节"));
            requestCover(oN.m_nId);
        }
        m_pCount->setText(
            vNovels.empty()
                ? m_nSection == 3 ? "还没有阅读记录，开始阅读后会自动出现在这里。"
                                  : "这里还没有小说，试试搜索或发现页。"
                : QString("%1 本小说%2")
                      .arg(vNovels.size())
                      .arg(m_nSection == 1 ? QString(" · 第 %1 页").arg(m_nPage) : QString()));
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
        if (!m_bUnlocked || !m_pCovers->image(nId).isNull())
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
        m_pCovers->request(nId, baBytes);
    }
    void ClientWindow::downloadedBooks()
    {
        std::vector<Novel> vResult;
        QDir oRoot(m_strDirectory);
        for (const auto& strSpace : oRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            QDir oNs(oRoot.filePath(strSpace));
            for (const auto& strName : oNs.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                bool bOk;
                int nId = strName.toInt(&bOk);
                if (!bOk || nId <= 0)
                    continue;
                auto strExpected = QString::fromStdString(m_pStore->bookDirectory(nId).u8string());
                if (QDir::cleanPath(strExpected) != QDir::cleanPath(oNs.filePath(strName)))
                    continue;
                try
                {
                    vResult.push_back(m_pStore->novel(nId));
                }
                catch (const std::exception&)
                {
                }
            }
        }
        populate(vResult);
    }
    void ClientWindow::openBook(int nId, bool bResume)
    {
        if (!m_bUnlocked || m_bLoadingBusy)
            return;
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
        if (m_pSession)
            m_pSession->openBook(nId);
#endif
    }
    void ClientWindow::showBook(const Novel& oNovel, const std::vector<Volume>& vVolumes,
                                bool bLocal)
    {
        if (!m_bUnlocked)
            return;
        m_pOperations->hide();
        m_pRefresh->hide();
        m_pTitle->setText("小说详情");
        m_pSubtitle->setText("选择章节开始，或从上次的位置继续。");
        m_oBook = oNovel;
        m_bLocalBook = bLocal;
        m_vFlat.clear();
        m_nChapter = -1;
        m_pToc->clear();
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
        m_pDetailTitle->setText(text(m_oBook.m_strTitle));
        m_pDetailMeta->setText(text(m_oBook.m_strAuthor) +
                               QString("   ·   %1 章").arg(m_vFlat.size()) +
                               (bLocal ? "   ·   已缓存" : ""));
        m_pDetailIntro->setText(text(m_oBook.m_strIntro).isEmpty()
                                    ? "故事已经准备好，选择一个章节开始阅读。"
                                    : text(m_oBook.m_strIntro));
        auto oSaved = m_pReadingState->position(m_oBook.m_nId);
        m_pRead->setText(oSaved ? "继续阅读" : "开始阅读");
        m_pRead->setToolTip(oSaved ? oSaved->m_strChapterTitle : QString());
        m_pRead->setEnabled(!m_vFlat.isEmpty());
        m_pDownload->setEnabled(!bLocal);
        m_pIllustrations->setVisible(!bLocal);
        auto oImage = m_pCovers->image(m_oBook.m_nId);
        m_pDetailCover->clear();
        if (!oImage.isNull())
            m_pDetailCover->setPixmap(coverPixmap(oImage, m_pDetailCover->size() - QSize(4, 6),
                                                  m_pDetailCover->devicePixelRatioF()));
        else
            m_pDetailCover->setText(m_pCovers->placeholder(m_oBook.m_nId));
        requestCover(m_oBook.m_nId);
        updateShelfAction();
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
        if (pTarget)
        {
            m_pToc->setCurrentItem(pTarget);
            m_pToc->scrollToItem(pTarget);
        }
    }
    void ClientWindow::openChapter(int nIndex)
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession && m_pSession->busy())
            return;
#endif
        if (!m_bUnlocked || nIndex < 0 || nIndex >= m_vFlat.size())
            return;
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
        if (!m_bUnlocked || m_nPendingChapter < 0 || m_nPendingChapter >= m_vFlat.size())
            return;
        m_bRestoring = true;
        const auto nGeneration = ++m_nRestoreGeneration;
        m_bReaderChrome = true;
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
        m_pReadTitle->setText(text(m_oBook.m_strTitle));
        updateReaderFooter();
        auto oSaved = m_pReadingState->position(m_oBook.m_nId);
        int nOffset = m_nRestoreOffset >= 0 ? m_nRestoreOffset
                      : oSaved && oSaved->m_nChapter == m_vFlat[m_nChapter].m_nId
                          ? oSaved->m_nOffset
                          : 0;
        m_nRestoreOffset = -1;
        m_pReader->restorePosition(nOffset);
        QTimer::singleShot(0, this,
                           [this, nGeneration, nOffset]
                           {
                               if (nGeneration != m_nRestoreGeneration || !m_bUnlocked ||
                                   m_pPages->currentIndex() != 3)
                                   return;
                               m_pReader->restorePosition(nOffset);
                               m_bRestoring = false;
                               saveProgress(true);
                               highlightChapter();
                           });
        message(m_bLocalBook ? "缓存阅读 · 阅读位置自动保存" : "在线阅读 · 阅读位置自动保存");
    }
    QString ClientWindow::progressKey() const
    {
        return "reading/" + m_strOrigin + "/" + QString::number(m_oBook.m_nId);
    }
    void ClientWindow::saveProgress(bool bVisited)
    {
        if (!m_bUnlocked || m_bRestoring || m_pPages->currentIndex() != 3 || m_nChapter < 0 ||
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
        if (!m_bUnlocked || m_bRestoring || m_nChapter < 0 || m_pPages->currentIndex() != 3)
            return;

        BookmarkEntry oEntry;
        oEntry.m_nChapter = m_vFlat[m_nChapter].m_nId;
        oEntry.m_nOffset = m_pReader->readingPosition();
        oEntry.m_strTitle = text(m_vFlat[m_nChapter].m_strTitle);
        oEntry.m_strSnippet = m_pReader->readingSnippet(oEntry.m_nOffset);
        m_pReadingState->addBookmark(m_oBook.m_nId, oEntry);
        message("书签已保存，可在「书签列表」管理和跳转。");
    }
    void ClientWindow::showBookmarks()
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession && m_pSession->busy())
            return;
#endif
        if (!m_bUnlocked || m_oBook.m_nId <= 0)
            return;
        saveProgress();
        if (mobileMode())
        {
            showMobileBookmarks();
            return;
        }
        QDialog oDialog(this);
        oDialog.setObjectName("bookmarksDialog");
        oDialog.setWindowTitle("书签 · " + text(m_oBook.m_strTitle));
        oDialog.resize(qMin(620, width() - 24), qMin(460, height() - 40));
        auto pLayout = new QVBoxLayout(&oDialog);
        pLayout->setContentsMargins(24, 24, 24, 24);
        pLayout->setSpacing(14);
        pLayout->addWidget(label("这本书的书签", "bookTitle"));
        auto pList = new QListWidget;
        pList->setObjectName("bookmarkItems");
        pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QScroller::grabGesture(pList->viewport(), QScroller::TouchGesture);
        pList->setWordWrap(true);
        pLayout->addWidget(pList, 1);
        auto pFooter = new QHBoxLayout;
        auto pRemove = button("删除书签", "removeBookmark"),
             pJump = button("跳转阅读", "jumpBookmark"), pClose = button("关闭");
        pJump->setProperty("primary", true);
        pFooter->addWidget(pRemove);
        pFooter->addStretch();
        pFooter->addWidget(pClose);
        pFooter->addWidget(pJump);
        pLayout->addLayout(pFooter);
        QVector<BookmarkEntry> vEntries;
        auto fnReload = [&]
        {
            pList->clear();
            vEntries = m_pReadingState->bookmarks(m_oBook.m_nId);
            for (const auto& oEntry : vEntries)
            {
                auto pItem = new QListWidgetItem(
                    oEntry.m_strTitle + "\n" + oEntry.m_strSnippet + "\n" +
                        oEntry.m_oCreated.toLocalTime().toString("yyyy-MM-dd HH:mm"),
                    pList);
                pItem->setSizeHint(QSize(240, 100));
                pItem->setData(Qt::UserRole, oEntry.m_strId);
                pItem->setData(Qt::UserRole + 1, oEntry.m_nOffset);
            }
            if (!vEntries.isEmpty())
                pList->setCurrentRow(0);
            pRemove->setEnabled(!vEntries.isEmpty());
            pJump->setEnabled(!vEntries.isEmpty());
            if (vEntries.isEmpty())
            {
                auto pItem = new QListWidgetItem(
                    "还没有书签。在阅读页点击「添加书签」记录当前位置。", pList);
                pItem->setFlags(Qt::NoItemFlags);
            }
        };
        int nTargetChapter = 0, nTargetOffset = 0;
        auto fnSelect = [&]
        {
            int nRow = pList->currentRow();
            if (nRow < 0 || nRow >= vEntries.size())
                return;
            nTargetChapter = vEntries[nRow].m_nChapter;
            nTargetOffset = vEntries[nRow].m_nOffset;
            oDialog.accept();
        };
        connect(pJump, &QPushButton::clicked, &oDialog, fnSelect);
        connect(pList, &QListWidget::itemDoubleClicked, &oDialog,
                [&](QListWidgetItem*)
                {
                    fnSelect();
                });
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::reject);
        connect(pRemove, &QPushButton::clicked, &oDialog,
                [&]
                {
                    int nRow = pList->currentRow();
                    if (nRow >= 0 && nRow < vEntries.size())
                    {
                        m_pReadingState->removeBookmark(m_oBook.m_nId, vEntries[nRow].m_strId);
                        fnReload();
                    }
                });
        fnReload();
        applyTheme(&oDialog, m_eTheme);
        if (oDialog.exec() == QDialog::Accepted)
        {
            for (int nI = 0; nI < m_vFlat.size(); ++nI)
                if (m_vFlat[nI].m_nId == nTargetChapter)
                {
                    m_nRestoreOffset = nTargetOffset;
                    openChapter(nI);
                    return;
                }
            message("这个书签的章节已不在当前目录中。");
        }
    }
    void ClientWindow::updateShelfAction()
    {
        const bool bSaved = m_vShelfIds.contains(m_oBook.m_nId);
        m_pShelfAdd->setText(bSaved ? "已在书架" : "加入书架");
        m_pShelfAdd->setToolTip(bSaved ? "可以在「我的书架」中找到这本小说"
                                       : "添加到文库网站的个人书架");
        bool bBusy = false;
#ifdef WENKU8_CLIENT_ONLINE
        bBusy = m_pSession && m_pSession->busy();
#endif
        m_pShelfAdd->setEnabled(m_bUnlocked && m_oBook.m_nId > 0 && !bSaved && !bBusy);
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
        m_bLoadingBusy = bBusy;
        for (auto pShortcut : findChildren<QShortcut*>())
            pShortcut->setEnabled(!bBusy);
        if (m_pDesktopMenuBar)
            m_pDesktopMenuBar->setEnabled(!bBusy);
        auto pShell = findChild<QWidget*>("shell");
        if (bBusy)
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
            m_pLoading->setGeometry(m_pStartupPages->rect());
            m_pLoading->show();
            m_pLoading->raise();
            m_pLoading->setFocus(Qt::OtherFocusReason);
        }
        else
        {
            pShell->setEnabled(true);
            m_pLoading->hide();
            m_pCardsRefresh->setRefreshing(false);
            m_pTocRefresh->setRefreshing(false);
            if (m_pLoadingFocus && m_pLoadingFocus->isVisible() && m_pLoadingFocus->isEnabled())
                m_pLoadingFocus->setFocus(Qt::OtherFocusReason);
            m_pLoadingFocus = nullptr;
        }
        updateShelfAction();
        m_pOperations->setEnabled(m_bUnlocked && !bBusy);
        m_pRefresh->setEnabled(m_bUnlocked && !bBusy);
        for (auto pB : m_vNav)
            pB->setEnabled(m_bUnlocked && !bBusy);
        for (auto pB : m_vMobileNav)
            pB->setEnabled(m_bUnlocked && !bBusy);
        m_pRead->setEnabled(!bBusy && !m_vFlat.isEmpty());
        m_pDownload->setEnabled(m_bUnlocked && !bBusy && !m_bLocalBook && m_oBook.m_nId > 0);
        m_pLocalDownload->setEnabled(m_bUnlocked && !bBusy && m_oBook.m_nId > 0);
        m_pToc->setEnabled(!bBusy);
        m_pPrevious->setEnabled(
            !bBusy && (m_nChapter > 0 || (m_pReader->readingMode() == ReadingMode::Paged &&
                                          m_pReader->pageNumber() > 1)));
        m_pNext->setEnabled(!bBusy && (m_nChapter + 1 < m_vFlat.size() ||
                                       (m_pReader->readingMode() == ReadingMode::Paged &&
                                        m_pReader->pageNumber() < m_pReader->pageCount())));
        m_pCards->setEnabled(!bBusy);
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
            m_bUnlocked && !m_bLoadingBusy && m_nRetryContext == m_nBusinessContext;
        const bool bRetryVisible = bRecoverable && bRetry;
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
            m_pDownloadTasks->setVisible(m_bUnlocked && m_nSection == 2 && m_pPages->currentIndex() == 1);
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
        // A maximized window already respects the platform's available area. When
        // fullscreen extends under system UI, inset only the part outside that area.
        if (windowHandle() && screen())
        {
            QRect oGlobal(mapToGlobal(QPoint(0, 0)), size());
            QRect oSafe = oGlobal.intersected(screen()->availableGeometry());
            if (!oSafe.isEmpty())
                centralWidget()->layout()->setContentsMargins(
                    qMax(0, oSafe.left() - oGlobal.left()), qMax(0, oSafe.top() - oGlobal.top()),
                    qMax(0, oGlobal.right() - oSafe.right()),
                    qMax(0, oGlobal.bottom() - oSafe.bottom()));
        }
#endif
        const bool bMobile = mobileMode(), bReading = m_pPages->currentIndex() == 3,
                   bExtra = m_pPages->currentIndex() >= 4;
        if (m_pDesktopMenuBar)
            m_pDesktopMenuBar->setVisible(!bMobile && !m_bReaderFullscreen &&
                                          m_pStartupPages->currentIndex() == 1);
        const bool bTab = bExtra && m_pPages->currentWidget()->property("rootTab").toBool();
        const bool bLandscape = bMobile && height() < 520;
        if (property("touchUi").toBool() != bMobile)
        {
            setProperty("touchUi", bMobile);
            applyTheme(this, m_eTheme);
        }
        m_pIllustrations->setTouchUi(bMobile);
        if (m_pCards->property("mobileCards").toBool() != bMobile)
        {
            m_pCards->setProperty("mobileCards", bMobile);
            m_pCards->doItemsLayout();
        }
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
        m_pMobileNavigation->setVisible(bMobile && !bReading && (!bExtra || bTab));
        m_pMobileTitle->setText(bExtra
                                    ? m_pPages->currentWidget()->property("mobileTitle").toString()
                                : m_pPages->currentIndex() == 1 ? m_pTitle->text()
                                                                : applicationName());
        findChild<QPushButton*>("mobileAccount")->hide();
        m_pHeading->setVisible(!m_bReaderFullscreen && !bMobile);
        m_pSubtitle->setVisible(!bMobile);
        m_pHeading->findChild<QLabel*>("eyebrow")->setVisible(!bMobile);
        findChild<QPushButton*>("mobileBack")->setVisible(m_pPages->currentIndex() >= 2 && !bTab);
        findChild<QPushButton*>("detailBack")->setVisible(!bMobile);
        m_pBodyLayout->setContentsMargins(bMobile ? 12 : 36, bMobile ? 8 : 30, bMobile ? 12 : 36,
                                          bMobile ? 8 : 18);
        m_pBodyLayout->setSpacing(bMobile ? 8 : 20);
        m_pDetailLayout->setContentsMargins(bMobile ? 12 : 32, bMobile ? 10 : 28, bMobile ? 12 : 32,
                                            bMobile ? 10 : 28);
        m_pDetailLayout->setSpacing(bMobile ? 8 : 16);
        m_pDetailHeaderLayout->setSpacing(bMobile ? 12 : 26);
        const QSize oCoverSize(bMobile ? 80 : 132, bMobile ? 116 : 192);
        const bool bCoverResized = m_pDetailCover->size() != oCoverSize;
        m_pDetailCover->setFixedSize(oCoverSize);
        if (bCoverResized)
        {
            if (auto oImage = m_pCovers->image(m_oBook.m_nId); !oImage.isNull())
            {
                m_pDetailCover->setPixmap(coverPixmap(oImage, m_pDetailCover->size() - QSize(4, 6),
                                                      m_pDetailCover->devicePixelRatioF()));
            }
        }
        m_pDetailIntro->setVisible(!bMobile);
        findChild<QPushButton*>("showSynopsis")->setVisible(bMobile);
        auto* pDetail = m_pDetailTitle->parentWidget();
        if (!pDetail->property("mobileDetailActions").isValid() ||
            pDetail->property("mobileDetailActions").toBool() != bMobile)
        {
            pDetail->setProperty("mobileDetailActions", bMobile);
            while (auto* pItem = m_pDetailActions->takeAt(0))
            {
                delete pItem;
            }
            auto* pSynopsis = findChild<QPushButton*>("showSynopsis");
            m_pDetailActions->addWidget(m_pRead, 0, 0);
            m_pDetailActions->addWidget(m_pShelfAdd, 0, 1);
            m_pDetailActions->addWidget(m_pDownload, 1, 0);
            m_pDetailActions->addWidget(m_pLocalDownload, 1, 1);
            m_pDetailActions->addWidget(m_pIllustrations, bMobile ? 2 : 1, bMobile ? 0 : 2);
            m_pDetailActions->addWidget(pSynopsis, bMobile ? 2 : 0, bMobile ? 1 : 2);
            for (int nColumn = 0; nColumn < 3; ++nColumn)
            {
                m_pDetailActions->setColumnStretch(nColumn, nColumn < (bMobile ? 2 : 3) ? 1 : 0);
            }
        }
        const auto oColors = colors(m_eTheme);
        const QString strDetailStyle = QString("font-size:%1px;font-weight:600;color:%2;")
            .arg(bMobile ? 18 : 25).arg(oColors.m_oText.name());
        const QString strTitleStyle = QString("font-size:%1px;font-weight:600;color:%2;")
            .arg(bMobile ? 22 : 28).arg(oColors.m_oText.name());
        if (m_pDetailTitle->styleSheet() != strDetailStyle)
        {
            m_pDetailTitle->setStyleSheet(strDetailStyle);
        }
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
        m_pReadTitle->setAlignment(bMobile ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
        for (auto pAction : {findChild<QPushButton*>("readerBack"), m_pReaderToolsToggle})
        {
            pAction->setMinimumWidth(bMobile ? 64 : 0);
            pAction->setMaximumWidth(bMobile ? 64 : QWIDGETSIZE_MAX);
        }
        m_pReaderTools->setVisible(!bMobile);
        m_pFullscreen->setVisible(!bMobile);
        m_pFullscreen->setText(m_bReaderFullscreen ? "退出全屏" : "全屏阅读");
        findChild<QWidget*>("readerTop")->setVisible(!bMobile || m_bReaderChrome);
        findChild<QWidget*>("readerFooter")->setVisible(!bMobile || m_bReaderChrome);
        if (bMobile)
            m_pOperations->setVisible(m_bUnlocked && m_pPages->currentIndex() == 1 &&
                                      m_nSection < 2);
        m_pRefresh->setVisible(
            !bMobile && m_bUnlocked &&
            (m_pPages->currentIndex() == 1 || (m_pPages->currentIndex() == 2 && !m_bLocalBook)));
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
        updateVolumePaging();
        m_pCards->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (bMobile)
        {
            for (auto pControl : findChildren<QPushButton*>())
                pControl->setMinimumHeight(pControl->objectName() == "themeChoice" ? 80
                                           : pControl->objectName() == "mobileNav" ? 64
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
        if (m_pPages->currentIndex() == 2)
            navigate(m_nSection);
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
        if (m_bUnlocked && m_bVolumePaging && m_pPages->currentIndex() == 3 &&
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
        if (pEvent->key() == Qt::Key_Back && m_pPages->currentIndex() <= 1 && !m_bReaderFullscreen)
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
    void ClientWindow::showAccountSheet()
    {
        if (mobileMode())
        {
            showMobileAccount();
            return;
        }
        QDialog oDialog(this);
        oDialog.setObjectName("accountSheet");
        oDialog.setWindowTitle("外观与账号");
        auto pLayout = new QVBoxLayout(&oDialog);
        pLayout->setContentsMargins(20, 20, 20, 20);
        pLayout->setSpacing(16);
        pLayout->addWidget(label("外观与账号", "bookTitle"));
        auto pAccount = label(m_pAccount->text(), "muted");
        pAccount->setWordWrap(true);
        pLayout->addWidget(pAccount);
        auto pThemes = new ThemeSelector;
        pThemes->setObjectName("themeSelector");
        pThemes->addItems({"明亮", "暗色", "护眼"});
        pThemes->setCurrentIndex(int(m_eTheme));
        pLayout->addWidget(pThemes);
        connect(pThemes, QOverload<int>::of(&QComboBox::currentIndexChanged), &oDialog,
                [this, &oDialog](int nIndex)
                {
                    setTheme(Theme(nIndex));
                    applyTheme(&oDialog, m_eTheme);
                });
        auto pLogin = button(m_pLogin->text());
        pLayout->addWidget(pLogin);
        connect(pLogin, &QPushButton::clicked, &oDialog,
                [this, &oDialog]
                {
                    oDialog.accept();
                    m_pLogin->click();
                });
        auto pClose = button("完成");
        pLayout->addWidget(pClose);
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        applyTheme(&oDialog, m_eTheme);
        const QStringList vInformationTitles{"版本说明", "更新日志", "开源许可"};
        for (int nI = 0; nI < vInformationTitles.size(); ++nI)
        {
            auto pInfo = button(vInformationTitles[nI]);
            pLayout->insertWidget(pLayout->count() - 1, pInfo);
            connect(pInfo, &QPushButton::clicked, &oDialog,
                    [this, nI]
                    {
                        showAppInformation(nI);
                    });
        }
        for (auto pAction : oDialog.findChildren<QPushButton*>())
            pAction->setMinimumHeight(44);
        oDialog.resize(qMin(420, width() - 24), 300);
        oDialog.exec();
    }
    void ClientWindow::closeEvent(QCloseEvent* pEvent)
    {
        saveProgress();
        m_oSettings.sync();
        QMainWindow::closeEvent(pEvent);
    }
    void ClientWindow::resizeEvent(QResizeEvent* pEvent)
    {
        QMainWindow::resizeEvent(pEvent);
        if (m_pReader)
            adaptLayout();
        if (m_pLoading)
            m_pLoading->setGeometry(m_pStartupPages->rect());
    }
} // namespace Wenku8::Reader
