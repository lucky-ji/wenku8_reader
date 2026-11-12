// SPDX-License-Identifier: GPL-2.0-only
#include "Reader.h"
#include "RasterImage.h"
#include <QImage>
#include <QBuffer>
#include <QImageReader>
#include <QScroller>
#include <QScopedValueRollback>
#include <QTextCursor>
#include <QScrollBar>
#include <QResizeEvent>
#include <QShowEvent>
#include <QUrl>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <algorithm>
#include <QAbstractTextDocumentLayout>
#include <QTextLayout>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QtMath>
namespace Wenku8::Reader
{
    std::filesystem::path nativePath(const QString& strPath)
    {
#ifdef _WIN32
        return std::filesystem::path(strPath.toStdWString());
#else
        return std::filesystem::u8path(strPath.toUtf8().constData());
#endif
    }
    namespace
    {
        class NoNetwork final : public HttpTransport
        {
            HttpResponse perform(const HttpRequest&, const TransferOptions&) override
            {
                throw Error(ErrorCode::Unsupported, "Offline reader cannot perform HTTP");
            }
            void clearCookies() override
            {
            }
        };
        QString escaped(const QString& strText)
        {
            QString strResult = strText.toHtmlEscaped();
            strResult.replace("\r\n", "\n");
            strResult.replace('\r', '\n');
            strResult.replace('\n', "<br/>");
            return strResult;
        }
    } // namespace
    std::unique_ptr<NovelDownloader> openStore(const QString& strPath, const QString& strOrigin)
    {
        ClientOptions oSource;
        oSource.m_strEndpoint = strOrigin.toStdString();
        oSource.m_eProtocol = Protocol::Website;
        DownloadStoreOptions oStore;
        oStore.m_oDirectory = nativePath(strPath);
        oStore.m_vImageOrigins.clear();
        auto pDisabled = std::make_shared<NoNetwork>();
        return std::make_unique<NovelDownloader>(oSource, oStore, pDisabled, pDisabled);
    }
    ChapterText formatChapter(const QString& strTitle, const QString& strText)
    {
        ChapterText oOut;
        oOut.m_strHtml = "<h2>" + strTitle.toHtmlEscaped() + "</h2><p>";
        const QString strMarker = "<!--image-->";
        int nCursor = 0;
        while (nCursor < strText.size())
        {
            int nBegin = strText.indexOf(strMarker, nCursor);
            if (nBegin < 0)
            {
                oOut.m_strHtml += escaped(strText.mid(nCursor));
                break;
            }
            int nEnd = strText.indexOf(strMarker, nBegin + strMarker.size());
            if (nEnd < 0)
            {
                oOut.m_strHtml += escaped(strText.mid(nCursor));
                break;
            }
            oOut.m_strHtml += escaped(strText.mid(nCursor, nBegin - nCursor));
            // Remote URL is only a lookup key; never passed to QTextDocument or a browser.
            oOut.m_vImages.append(
                strText.mid(nBegin + strMarker.size(), nEnd - nBegin - strMarker.size()).trimmed());
            oOut.m_strHtml += QString("</p><p align='center'><img src='wenku8-local:/%1'/></p><p>")
                                  .arg(oOut.m_vImages.size() - 1);
            nCursor = nEnd + strMarker.size();
        }
        oOut.m_strHtml += "</p>";
        return oOut;
    }
    LocalDocument::LocalDocument(QObject* pParent) : QTextDocument(pParent)
    {
    }
    void LocalDocument::setChapter(const ChapterText& oText, ImageLoader fnLoader, int nWidth,
                                   int nHeight, int nLineHeightPercent, int nParagraphSpacing)
    {
        // Clear previous resource cache, including same-numbered images of the previous chapter.
        clear();
        m_vImages = oText.m_vImages;
        m_fnLoader = std::move(fnLoader);
        m_nImageWidth = std::max(1, nWidth);
        m_nImageHeight = nHeight;
        m_nLoadedImages = 0;
        setUndoRedoEnabled(false);
        setHtml(oText.m_strHtml);
        // Convert visual line separators into blocks one-for-one. Both are one UTF-16
        // character and toPlainText() maps both to '\n', preserving saved anchors.
        QTextCursor oEdit(this);
        oEdit.beginEditBlock();
        const QString strRaw = toRawText();
        for (int nPosition = strRaw.indexOf(QChar::LineSeparator); nPosition >= 0;
             nPosition = strRaw.indexOf(QChar::LineSeparator, nPosition + 1))
        {
            oEdit.setPosition(nPosition);
            oEdit.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            auto oBlock = oEdit.blockFormat();
            auto oCharacters = oEdit.charFormat();
            oEdit.insertBlock(oBlock, oCharacters);
        }
        bool bPreviousBlank = false;
        for (auto oBlock = begin(); oBlock.isValid(); oBlock = oBlock.next())
        {
            QTextCursor oCursor(oBlock);
            auto oFormat = oBlock.blockFormat();
            const auto strContent = oBlock.text();
            bool bBlank = strContent.trimmed().isEmpty();
            oFormat.setTopMargin(0);
            oFormat.setBottomMargin(0);
            if (bBlank)
            {
                oFormat.setLineHeight(bPreviousBlank ? 1 : std::clamp(nParagraphSpacing, 1, 24),
                                      QTextBlockFormat::FixedHeight);
            }
            else if (strContent.contains(QChar::ObjectReplacementCharacter))
            {
                oFormat.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
                oFormat.setTopMargin(10);
                oFormat.setBottomMargin(10);
            }
            else if (oFormat.headingLevel() > 0)
            {
                oFormat.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
                oFormat.setBottomMargin(18);
            }
            else
            {
                oFormat.setLineHeight(std::clamp(nLineHeightPercent, 120, 220),
                                      QTextBlockFormat::ProportionalHeight);
            }
            oCursor.setBlockFormat(oFormat);
            bPreviousBlank = bBlank;
        }
        oEdit.endEditBlock();
    }
    QVariant LocalDocument::loadResource(int nType, const QUrl& oName)
    {
        if (nType != QTextDocument::ImageResource || oName.scheme() != "wenku8-local" ||
            !oName.host().isEmpty())
            return {};
        bool bOk = false;
        int nIndex = oName.path().mid(1).toInt(&bOk);
        if (!bOk || nIndex < 0 || nIndex >= m_vImages.size() || !m_fnLoader)
            return {};
        try
        {
            QByteArray baBytes = m_fnLoader(m_vImages[nIndex]);
            if (baBytes.isEmpty() || baBytes.size() > 32 * 1024 * 1024)
                return {};
            const auto baFormat = rasterFormat(baBytes);
            if (baFormat.isEmpty())
                return {};
            QBuffer oBuffer(&baBytes);
            if (!oBuffer.open(QIODevice::ReadOnly))
                return {};
            QImageReader oReader(&oBuffer, baFormat);
            oReader.setDecideFormatFromContent(false);
            const auto oSource = oReader.size();
            if (oSource.isEmpty() || qint64(oSource.width()) * oSource.height() > 20000000)
                return {};
            auto oTarget = oSource;
            if (m_nImageHeight > 0 &&
                (oSource.width() > m_nImageWidth || oSource.height() > m_nImageHeight))
                oTarget = oSource.scaled(QSize(m_nImageWidth, m_nImageHeight), Qt::KeepAspectRatio);
            else if (oSource.width() > m_nImageWidth)
                oTarget = QSize(m_nImageWidth, qMax(1, qRound(qreal(oSource.height()) *
                                                              m_nImageWidth / oSource.width())));
            // Qt's JPEG handler delegates downscaling to libjpeg before allocating
            // the full image. Keep other formats on their existing decode/scale path.
            // Do not set autoTransform: chapter images retain their existing layout.
            if (baFormat == "jpeg" && oTarget != oSource)
                oReader.setScaledSize(oTarget);
            QImage oImage = oReader.read();
            if (oImage.isNull() || qint64(oImage.width()) * oImage.height() > 20000000)
                return {};
            if (oImage.size() != oTarget)
                oImage = oImage.scaled(oTarget, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (oImage.isNull())
                return {};
            ++m_nLoadedImages;
            addResource(nType, oName, oImage);
            return oImage;
        }
        catch (const std::exception&)
        {
            return {};
        }
    }
    ReadingView::ReadingView(QWidget* pParent)
        : QTextBrowser(pParent), m_pDocument(new LocalDocument(this))
    {
        setDocument(m_pDocument);
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setReadOnly(true);
        setLineWrapMode(QTextEdit::WidgetWidth);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
        m_oWarmTimer.setSingleShot(true);
        m_oWarmTimer.setInterval(0);
        connect(&m_oWarmTimer, &QTimer::timeout, this,
                [this]
                {
                    warmNextBand();
                });
        m_oImageRefreshTimer.setSingleShot(true);
        m_oImageRefreshTimer.setInterval(100);
        connect(&m_oImageRefreshTimer, &QTimer::timeout, this,
                [this]
                {
                    if (m_bImagesPending)
                        refreshImages();
                });
    }
    bool ReadingView::viewportEvent(QEvent* pEvent)
    {
        const bool bResult = QTextBrowser::viewportEvent(pEvent);
        // QStyleSheetStyle can clear opacity during polish/repolish. Our paintEvent
        // supplies an opaque paper background, so scrolling may safely reuse pixels.
        if (pEvent->type() == QEvent::Polish || pEvent->type() == QEvent::StyleChange)
        {
            viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
            clearRasterCache();
        }
        return bResult;
    }
    void ReadingView::scrollContentsBy(int nDx, int nDy)
    {
        viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
        // Page masks belong to viewport coordinates and must not be bit-blitted.
        if (nDy)
            m_nWarmDirection = nDy < 0 ? 1 : -1;
        if (m_eMode == ReadingMode::Paged)
        {
            viewport()->update();
            queueWarmBands();
            return;
        }
        // Read-only mobile reading has no editable cursor or selection handles.
        // QTextEdit also notifies QInputMethod on every scroll; avoid that work.
        if (textInteractionFlags() == Qt::NoTextInteraction)
        {
            viewport()->scroll(isRightToLeft() ? -nDx : nDx, nDy);
            queueWarmBands();
            return;
        }
        QTextBrowser::scrollContentsBy(nDx, nDy);
    }
    void ReadingView::showChapter(const QString& strTitle, const QString& strText,
                                  LocalDocument::ImageLoader fnLoader)
    {
        m_oChapter = formatChapter(strTitle, strText);
        m_fnLoader = std::move(fnLoader);
        rebuild(0);
    }
    void ReadingView::rebuild(int nPosition)
    {
        if (m_bRebuilding)
            return;
        if (!isVisible() && !m_oChapter.m_strHtml.isEmpty())
        {
            // Layout changes from hidden settings pages and late illustration
            // replies collapse into one rebuild at the final visible geometry.
            m_bLayoutPending = true;
            m_nPendingPosition = nPosition;
            clearRasterCache();
            m_oImageRefreshTimer.stop();
            return;
        }
        m_bLayoutPending = false;
        cancelMotion();
        clearRasterCache();
        m_oImageRefreshTimer.stop();
        m_bImagesPending = false;
        QScopedValueRollback<bool> oGuard(m_bRebuilding, true);
        // A fresh document also guarantees no cached image survives chapter changes.
        QPointer<LocalDocument> pPrevious = m_pDocument;
        m_pDocument = new LocalDocument(this);
        setDocument(m_pDocument);
        if (pPrevious)
            pPrevious->deleteLater();
        m_pDocument->setDefaultFont(font());
        m_pDocument->setDefaultStyleSheet(
            "p { margin-top:0; margin-bottom:0; } h2 { margin-bottom:18px; }");
        const int nPreferredMargin =
            m_nSideMargin > 0 ? m_nSideMargin : viewport()->width() < 600 ? 16 : 24;
        const int nMargin = qMin(nPreferredMargin, qMax(0, (viewport()->width() - 80) / 2));
        m_pDocument->setDocumentMargin(nMargin);
        m_pDocument->setChapter(
            m_oChapter, m_fnLoader, std::max(1, viewport()->width() - 2 * nMargin - 8),
            m_eMode == ReadingMode::Paged ? qMax(1, viewport()->height() - 2 * nMargin - 24) : 0,
            m_nLineHeightPercent, m_nParagraphSpacing);
        m_pDocument->setTextWidth(viewport()->width());
        if (m_eMode == ReadingMode::Paged)
        {
            auto oBlock = m_pDocument->lastBlock();
            QTextCursor oCursor(oBlock);
            auto oFormat = oBlock.blockFormat();
            oFormat.setBottomMargin(viewport()->height());
            oCursor.setBlockFormat(oFormat);
        }
        paginate();
        m_oLayoutViewport = viewport()->size();
        restorePosition(nPosition);
        queueWarmBands();
    }
    void ReadingView::reflow(int nPosition)
    {
        if (!isVisible())
        {
            rebuild(nPosition);
            return;
        }
        cancelMotion();
        clearRasterCache();
        QScopedValueRollback<bool> oGuard(m_bRebuilding, true);
        const int nPreferredMargin =
            m_nSideMargin > 0 ? m_nSideMargin : viewport()->width() < 600 ? 16 : 24;
        const int nMargin = qMin(nPreferredMargin, qMax(0, (viewport()->width() - 80) / 2));
        m_pDocument->setDocumentMargin(nMargin);
        m_pDocument->setTextWidth(viewport()->width());
        if (m_eMode == ReadingMode::Paged)
        {
            QTextCursor oCursor(m_pDocument->lastBlock());
            auto oFormat = oCursor.blockFormat();
            oFormat.setBottomMargin(viewport()->height());
            oCursor.setBlockFormat(oFormat);
        }
        paginate();
        m_oLayoutViewport = viewport()->size();
        restorePosition(nPosition);
    }
    int ReadingView::readingPosition() const
    {
        if (m_bLayoutPending)
        {
            return m_nPendingPosition;
        }
        if (m_eMode == ReadingMode::Paged && !m_vPages.isEmpty())
            return m_vPages[m_nPage].m_nOffset;
        return cursorForPosition(QPoint(26, 2)).position();
    }
    QString ReadingView::readingSnippet(int nPosition, int nLength) const
    {
        QTextCursor oCursor(m_pDocument);
        const int nEnd = qMax(0, m_pDocument->characterCount() - 1);
        oCursor.setPosition(qBound(0, nPosition, nEnd));
        oCursor.setPosition(int(qMin(qint64(nEnd), qint64(oCursor.position()) + qMax(0, nLength))),
                            QTextCursor::KeepAnchor);
        return oCursor.selectedText().simplified();
    }
    void ReadingView::restorePosition(int nPosition)
    {
        if (m_bLayoutPending)
        {
            m_nPendingPosition = qMax(0, nPosition);
            return;
        }
        cancelMotion();
        QScopedValueRollback<bool> oGuard(m_bRebuilding, true);
        QTextCursor oCursor(m_pDocument);
        oCursor.setPosition(
            std::clamp(nPosition, 0, std::max(0, m_pDocument->characterCount() - 1)));
        setTextCursor(oCursor);
        if (m_eMode == ReadingMode::Paged && !m_vPages.isEmpty())
        {
            m_nPage = 0;
            for (int nI = 1; nI < m_vPages.size(); ++nI)
                if (m_vPages[nI].m_nOffset <= oCursor.position())
                    m_nPage = nI;
                else
                    break;
            verticalScrollBar()->setValue(m_vPages[m_nPage].m_nY);
            viewport()->update();
            emit pageStateChanged();
            return;
        }
        verticalScrollBar()->setValue(verticalScrollBar()->value() + cursorRect(oCursor).top());
        emit pageStateChanged();
    }
    void ReadingView::setTypography(int nLineHeightPercent, int nParagraphSpacing, int nSideMargin)
    {
        nLineHeightPercent = std::clamp(nLineHeightPercent, 120, 220);
        nParagraphSpacing = std::clamp(nParagraphSpacing, 0, 24);
        nSideMargin = std::clamp(nSideMargin, 0, 48);
        if (m_nLineHeightPercent == nLineHeightPercent &&
            m_nParagraphSpacing == nParagraphSpacing && m_nSideMargin == nSideMargin)
        {
            return;
        }
        const int nPosition = readingPosition();
        m_nLineHeightPercent = nLineHeightPercent;
        m_nParagraphSpacing = nParagraphSpacing;
        m_nSideMargin = nSideMargin;
        if (!m_oChapter.m_strHtml.isEmpty())
        {
            rebuild(nPosition);
        }
    }
    void ReadingView::appearance(int nSize, Theme eTheme)
    {
        nSize = std::clamp(nSize, 10, 36);
        if (m_bAppearanceSet && font().pointSize() == nSize && m_eAppearanceTheme == eTheme)
        {
            return;
        }
        const int nPosition = readingPosition();
        const bool bFontChanged = font().pointSize() != nSize;
        m_bAppearanceSet = true;
        m_eAppearanceTheme = eTheme;
        cancelMotion();
        clearRasterCache();
        // Style polish can resize scroll bars. Handle the resulting final size
        // once after updating colors, rather than recursively recreating text.
        {
            QScopedValueRollback<bool> oGuard(m_bRebuilding, true);
            QFont oF = font();
            oF.setPointSize(nSize);
            if (bFontChanged)
            {
                setFont(oF);
            }
            const auto oC = colors(eTheme);
            m_oPaper = oC.m_oPaper;
            setStyleSheet("QTextBrowser { background:" + oC.m_oPaper.name() +
                          "; color:" + oC.m_oText.name() +
                          "; border:0; font-size:" + QString::number(nSize) +
                          "pt; }" + scrollBarStyle(eTheme));
            verticalScrollBar()->setStyleSheet(scrollBarStyle(eTheme));
            horizontalScrollBar()->setStyleSheet(scrollBarStyle(eTheme));
        }
        if (bFontChanged || m_bLayoutPending)
        {
            rebuild(nPosition);
        }
        else if (!m_oChapter.m_strHtml.isEmpty() && m_oLayoutViewport != viewport()->size())
        {
            reflow(nPosition);
        }
        viewport()->update();
    }
    void ReadingView::resizeEvent(QResizeEvent* pEvent)
    {
        int nPosition = readingPosition();
        QTextBrowser::resizeEvent(pEvent);
        if (m_bRebuilding || m_oChapter.m_strHtml.isEmpty())
        {
            return;
        }
        const bool bWidthChanged = viewport()->width() != m_oLayoutViewport.width();
        const bool bHeightChanged = viewport()->height() != m_oLayoutViewport.height();
        if (m_bLayoutPending || bWidthChanged || (bHeightChanged && m_eMode == ReadingMode::Paged))
        {
            if (m_bLayoutPending || !m_oChapter.m_vImages.isEmpty())
            {
                rebuild(nPosition);
            }
            else
            {
                // Width changes need line wrapping, not reparsing an entire
                // chapter. Paged text also needs new page boundaries on height.
                reflow(nPosition);
            }
        }
        else
        {
            m_oLayoutViewport = viewport()->size();
        }
    }
    namespace
    {
        class PageSlide final : public QWidget
        {
          public:
            QPixmap m_oBefore, m_oAfter;
            qreal m_fProgress = 0;
            int m_nDirection = 1;
            QColor m_oPaper;
            explicit PageSlide(QWidget* pParent) : QWidget(pParent)
            {
                setAttribute(Qt::WA_TransparentForMouseEvents);
                setAttribute(Qt::WA_OpaquePaintEvent);
            }
            void paintEvent(QPaintEvent*) override
            {
                QPainter oP(this);
                oP.fillRect(rect(), m_oPaper);
                oP.drawPixmap(QPoint(int(-m_nDirection * m_fProgress * width()), 0), m_oBefore);
                oP.drawPixmap(QPoint(int(m_nDirection * (1 - m_fProgress) * width()), 0), m_oAfter);
            }
        };
    } // namespace
    void ReadingView::cancelMotion()
    {
        if (m_pScrollAnimation)
        {
            m_pScrollAnimation->stop();
            m_pScrollAnimation->deleteLater();
            m_pScrollAnimation = nullptr;
        }
        m_nScrollTarget = -1;
        if (m_pTurnAnimation)
        {
            m_pTurnAnimation->disconnect(this);
            m_pTurnAnimation->stop();
            m_pTurnAnimation->deleteLater();
            m_pTurnAnimation = nullptr;
        }
        delete m_pPageOverlay;
        m_pPageOverlay = nullptr;
    }
    void ReadingView::setReadingMode(ReadingMode eMode)
    {
        if (m_eMode == eMode)
            return;
        const int nPosition = readingPosition();
        m_eMode = eMode;
        setVerticalScrollBarPolicy(eMode == ReadingMode::Paged ? Qt::ScrollBarAlwaysOff
                                                               : Qt::ScrollBarAsNeeded);
        rebuild(nPosition);
    }
    void ReadingView::paginate()
    {
        m_vPages.clear();
        m_nPage = 0;
        if (m_eMode != ReadingMode::Paged)
        {
            m_vPages.append({0, 0, viewport()->height()});
            return;
        }
        struct Line
        {
            int m_nTop, m_nBottom, m_nOffset;
        };
        QVector<Line> vLines;
        m_pDocument->documentLayout()->documentSize();
        for (auto oBlock = m_pDocument->begin(); oBlock.isValid(); oBlock = oBlock.next())
        {
            if (oBlock.text().trimmed().isEmpty())
                continue;
            auto pLayout = oBlock.layout();
            if (!pLayout)
                continue;
            const auto oBox = m_pDocument->documentLayout()->blockBoundingRect(oBlock);
            for (int nI = 0; nI < pLayout->lineCount(); ++nI)
            {
                auto oLine = pLayout->lineAt(nI);
                vLines.append({qFloor(oBox.top() + oLine.y()),
                               qCeil(oBox.top() + oLine.y() + oLine.height()),
                               oBlock.position() + oLine.textStart()});
            }
        }
        const int nInset = int(m_pDocument->documentMargin()),
                  nHeight = qMax(40, viewport()->height());
        int nStart = 0, nIndex = 0, nOffset = 0;
        while (nIndex < vLines.size())
        {
            int nNext = nIndex;
            while (nNext < vLines.size() && vLines[nNext].m_nBottom <= nStart + nHeight - nInset)
                ++nNext;
            if (nNext == nIndex)
                ++nNext; // Always make progress for an unusually tall line.
            if (nNext >= vLines.size())
            {
                m_vPages.append({nStart, nOffset, vLines.back().m_nBottom + nInset});
                break;
            }
            m_vPages.append({nStart, nOffset, vLines[nNext].m_nTop});
            nStart = qMax(nStart + 1, vLines[nNext].m_nTop - nInset);
            nOffset = vLines[nNext].m_nOffset;
            nIndex = nNext;
        }
        if (m_vPages.isEmpty())
            m_vPages.append({0, 0, nHeight});
        verticalScrollBar()->setRange(
            0, qMax(m_vPages.back().m_nY, qCeil(m_pDocument->size().height()) - nHeight));
    }
    bool ReadingView::turnPage(int nDirection)
    {
        nDirection = nDirection < 0 ? -1 : 1;
        if (m_eMode == ReadingMode::Paged)
        {
            int nTarget = m_nPage + nDirection;
            if (nTarget < 0 || nTarget >= m_vPages.size())
                return false;
            cancelMotion();
            const bool bMobile = textInteractionFlags() == Qt::NoTextInteraction;
            auto oBefore = bMobile ? pageSnapshot(m_nPage) : viewport()->grab();
            m_nPage = nTarget;
            m_nWarmDirection = nDirection;
            verticalScrollBar()->setValue(m_vPages[m_nPage].m_nY);
            auto oAfter = bMobile ? pageSnapshot(m_nPage) : viewport()->grab();
            auto pOverlay = new PageSlide(viewport());
            pOverlay->m_oPaper = m_oPaper;
            pOverlay->m_oBefore = oBefore;
            pOverlay->m_oAfter = oAfter;
            pOverlay->m_nDirection = nDirection;
            pOverlay->setGeometry(viewport()->rect());
            m_pPageOverlay = pOverlay;
            pOverlay->show();
            pOverlay->raise();
            m_pTurnAnimation = new QVariantAnimation(this);
            m_pTurnAnimation->setStartValue(0.0);
            m_pTurnAnimation->setEndValue(1.0);
            m_pTurnAnimation->setDuration(160);
            m_pTurnAnimation->setEasingCurve(QEasingCurve::OutCubic);
            connect(m_pTurnAnimation, &QVariantAnimation::valueChanged, this,
                    [pOverlay](const QVariant& oValue)
                    {
                        pOverlay->m_fProgress = oValue.toReal();
                        pOverlay->update();
                    });
            connect(m_pTurnAnimation, &QVariantAnimation::finished, this,
                    [this]
                    {
                        cancelMotion();
                    });
            m_pTurnAnimation->start();
            queueWarmBands();
            emit pageStateChanged();
            return true;
        }
        int nOrigin = m_nScrollTarget >= 0 ? m_nScrollTarget : verticalScrollBar()->value();
        const int nStep = qMax(1, viewport()->height() - 2 * fontMetrics().lineSpacing());
        int nTarget = qBound(0, nOrigin + nDirection * nStep, verticalScrollBar()->maximum());
        if (nTarget == nOrigin)
            return false;
        cancelMotion();
        m_nScrollTarget = nTarget;
        m_pScrollAnimation = new QPropertyAnimation(verticalScrollBar(), "value", this);
        m_pScrollAnimation->setDuration(180);
        m_pScrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_pScrollAnimation->setStartValue(verticalScrollBar()->value());
        m_pScrollAnimation->setEndValue(nTarget);
        connect(m_pScrollAnimation, &QPropertyAnimation::finished, this,
                [this]
                {
                    m_nScrollTarget = -1;
                });
        m_pScrollAnimation->start();
        return true;
    }
    int ReadingView::chapterProgress() const
    {
        if (m_eMode == ReadingMode::Paged)
        {
            return m_vPages.size() > 1 ? int(qint64(m_nPage) * 1000 / (m_vPages.size() - 1)) : 0;
        }
        const auto* pBar = verticalScrollBar();
        return pBar->maximum() > 0 ? int(qint64(pBar->value()) * 1000 / pBar->maximum()) : 0;
    }
    bool ReadingView::canSeekProgress() const
    {
        return !m_bLayoutPending && (m_eMode == ReadingMode::Paged
                                         ? m_vPages.size() > 1 : verticalScrollBar()->maximum() > 0);
    }
    void ReadingView::seekChapterProgress(int nPermille)
    {
        if (!canSeekProgress())
        {
            return;
        }
        nPermille = qBound(0, nPermille, 1000);
        cancelMotion();
        if (m_eMode == ReadingMode::Paged)
        {
            const int nPage = int((qint64(nPermille) * (m_vPages.size() - 1) + 500) / 1000);
            restorePosition(m_vPages[nPage].m_nOffset);
        }
        else
        {
            verticalScrollBar()->setValue(int(qint64(nPermille) * verticalScrollBar()->maximum() / 1000));
        }
        emit pageStateChanged();
    }
    bool ReadingView::findInChapter(const QString& strQuery, bool bBackward)
    {
        if (strQuery.isEmpty())
        {
            return false;
        }
        const auto oFlags = bBackward ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
        auto oFound = m_pDocument->find(strQuery, textCursor(), oFlags);
        if (oFound.isNull())
        {
            QTextCursor oStart(m_pDocument);
            oStart.movePosition(bBackward ? QTextCursor::End : QTextCursor::Start);
            oFound = m_pDocument->find(strQuery, oStart, oFlags);
        }
        if (oFound.isNull())
        {
            return false;
        }
        cancelMotion();
        restorePosition(oFound.selectionStart());
        setTextCursor(oFound);
        if (m_eMode == ReadingMode::Paged && !m_vPages.isEmpty())
        {
            verticalScrollBar()->setValue(m_vPages[m_nPage].m_nY);
        }
        emit pageStateChanged();
        return true;
    }
    bool ReadingView::moving() const
    {
        const auto eState = QScroller::scroller(viewport())->state();
        return eState != QScroller::Inactive ||
               (m_pScrollAnimation && m_pScrollAnimation->state() == QAbstractAnimation::Running) ||
               (m_pTurnAnimation && m_pTurnAnimation->state() == QAbstractAnimation::Running);
    }
    void ReadingView::refreshImages()
    {
        if (m_oChapter.m_vImages.isEmpty())
            return;
        if (!isVisible())
        {
            rebuild(readingPosition());
            return;
        }
        if (moving())
        {
            m_bImagesPending = true;
            m_oImageRefreshTimer.start();
            return;
        }
        m_bImagesPending = false;
        m_oImageRefreshTimer.stop();
        rebuild(readingPosition());
    }
    void ReadingView::clearRasterCache()
    {
        m_oWarmTimer.stop();
        m_vWarmBands.clear();
        m_vRasterBands.clear();
        m_oRasterViewport = {};
        m_fRasterDpr = 0;
        m_nRasterRevision = -1;
    }
    void ReadingView::validateRasterCache()
    {
        const auto oSize = viewport()->size();
        const auto fDpr = viewport()->devicePixelRatioF();
        const auto nRevision = m_pDocument->revision();
        const auto oCursor = textCursor();
        const int nSelectionStart = oCursor.hasSelection() ? oCursor.selectionStart() : -1,
                  nSelectionEnd = oCursor.hasSelection() ? oCursor.selectionEnd() : -1;
        if (oSize != m_oRasterViewport || !qFuzzyCompare(fDpr, m_fRasterDpr) ||
            nRevision != m_nRasterRevision || nSelectionStart != m_nRasterSelectionStart ||
            nSelectionEnd != m_nRasterSelectionEnd)
        {
            clearRasterCache();
            m_oRasterViewport = oSize;
            m_fRasterDpr = fDpr;
            m_nRasterRevision = nRevision;
            m_nRasterSelectionStart = nSelectionStart;
            m_nRasterSelectionEnd = nSelectionEnd;
        }
    }
    QPixmap ReadingView::rasterBand(int nIndex)
    {
        validateRasterCache();
        if (auto pCached = m_vRasterBands.object(nIndex))
            return *pCached;
        const int nWidth = qMax(1, viewport()->width()), nTop = nIndex * m_nBandHeight;
        const auto fDpr = viewport()->devicePixelRatioF();
        QPixmap oBand(qCeil(nWidth * fDpr), qCeil(m_nBandHeight * fDpr));
        oBand.setDevicePixelRatio(fDpr);
        oBand.fill(m_oPaper);
        {
            QPainter oPainter(&oBand);
            oPainter.setRenderHint(QPainter::TextAntialiasing);
            oPainter.setRenderHint(QPainter::SmoothPixmapTransform);
            oPainter.translate(0, -nTop);
            QAbstractTextDocumentLayout::PaintContext oContext;
            oContext.palette = palette();
            oContext.clip = QRectF(0, nTop, nWidth, m_nBandHeight);
            if (textCursor().hasSelection())
            {
                QAbstractTextDocumentLayout::Selection oSelection;
                oSelection.cursor = textCursor();
                oSelection.format.setBackground(palette().brush(QPalette::Highlight));
                oSelection.format.setForeground(palette().brush(QPalette::HighlightedText));
                oContext.selections.append(oSelection);
            }
            oPainter.setClipRect(oContext.clip);
            m_pDocument->documentLayout()->draw(&oPainter, oContext);
        }
        const int nCost = qMax(
            1, int(qint64(oBand.width()) * oBand.height() * qMax(1, oBand.depth() / 8) / 1024));
        // Oversize display bands may be painted once but never exceed the cache cap.
        if (nCost <= m_vRasterBands.maxCost())
            m_vRasterBands.insert(nIndex, new QPixmap(oBand), nCost);
        return oBand;
    }
    void ReadingView::paintBands(QPainter& oPainter, const QRect& oDirty, int nDocumentY)
    {
        oPainter.fillRect(oDirty, m_oPaper);
        if (oDirty.isEmpty())
            return;
        const int nFirst = qMax(0, (oDirty.top() + nDocumentY) / m_nBandHeight);
        const int nLast = qMax(nFirst, (oDirty.bottom() + nDocumentY) / m_nBandHeight);
        for (int nIndex = nFirst; nIndex <= nLast; ++nIndex)
            oPainter.drawPixmap(
                QPointF(-horizontalScrollBar()->value(), nIndex * m_nBandHeight - nDocumentY),
                rasterBand(nIndex));
    }
    QPixmap ReadingView::pageSnapshot(int nIndex)
    {
        const auto fDpr = viewport()->devicePixelRatioF();
        QPixmap oPage(qCeil(viewport()->width() * fDpr), qCeil(viewport()->height() * fDpr));
        oPage.setDevicePixelRatio(fDpr);
        oPage.fill(m_oPaper);
        QPainter oPainter(&oPage);
        paintBands(oPainter, viewport()->rect(), m_vPages[nIndex].m_nY);
        const int nInset = int(m_pDocument->documentMargin()),
                  nEnd = qBound(nInset, m_vPages[nIndex].m_nEnd - m_vPages[nIndex].m_nY,
                                viewport()->height());
        oPainter.fillRect(QRect(0, 0, viewport()->width(), nInset), m_oPaper);
        oPainter.fillRect(QRect(0, nEnd, viewport()->width(), viewport()->height() - nEnd),
                          m_oPaper);
        return oPage;
    }
    void ReadingView::queueWarmBands()
    {
        if (!isVisible() || textInteractionFlags() != Qt::NoTextInteraction || m_bRebuilding ||
            m_bLayoutPending)
            return;
        validateRasterCache();
        m_vWarmBands.clear();
        const int nFirst = qMax(0, verticalScrollBar()->value() / m_nBandHeight),
                  nLast = qMax(nFirst, (verticalScrollBar()->value() + viewport()->height() - 1) /
                                           m_nBandHeight);
        // Visible bands come first, then a bounded look-ahead in the gesture's direction.
        for (int nIndex = nFirst; nIndex <= nLast; ++nIndex)
            if (!m_vRasterBands.contains(nIndex))
                m_vWarmBands.append(nIndex);
        const qint64 nBytesPerBand = qint64(qCeil(viewport()->width() * m_fRasterDpr)) *
                                     qCeil(m_nBandHeight * m_fRasterDpr) * 4;
        const int nCapacity =
            nBytesPerBand > 0 ? int(qint64(m_vRasterBands.maxCost()) * 1024 / nBytesPerBand) : 0;
        const int nAhead = qMax(0, qMin(nLast - nFirst + 1, nCapacity - (nLast - nFirst + 1)));
        const int nLimit = qMax(nLast, qCeil(m_pDocument->size().height() / m_nBandHeight));
        for (int nStep = 1; nStep <= nAhead; ++nStep)
        {
            const int nIndex = m_nWarmDirection > 0 ? nLast + nStep : nFirst - nStep;
            if (nIndex >= 0 && nIndex <= nLimit && !m_vRasterBands.contains(nIndex))
                m_vWarmBands.append(nIndex);
        }
        if (!m_vWarmBands.isEmpty() && !m_oWarmTimer.isActive())
            m_oWarmTimer.start();
    }
    void ReadingView::warmNextBand()
    {
        if (!isVisible() || textInteractionFlags() != Qt::NoTextInteraction ||
            m_vWarmBands.isEmpty())
            return;
        // Only one band per event-loop turn. Touch/paint/animation events can run
        // between bands, rather than paying for an entire next page at once.
        const int nIndex = m_vWarmBands.takeFirst();
        rasterBand(nIndex);
        if (!m_vWarmBands.isEmpty())
            m_oWarmTimer.start();
    }
    void ReadingView::paintEvent(QPaintEvent* pEvent)
    {
        if (textInteractionFlags() == Qt::NoTextInteraction)
        {
            // Rasterize document bands once at the display DPR. A drag then copies
            // cached pixels into exposed strips instead of repainting text glyphs.
            QPainter oPainter(viewport());
            oPainter.setClipRegion(pEvent->region());
            paintBands(oPainter, pEvent->rect(), verticalScrollBar()->value());
            queueWarmBands();
        }
        else
        {
            // Desktop selection/caret painting stays with QTextBrowser.
            {
                QPainter oBackground(viewport());
                oBackground.setClipRegion(pEvent->region());
                oBackground.fillRect(pEvent->rect(), m_oPaper);
            }
            QTextBrowser::paintEvent(pEvent);
        }
        if (m_eMode != ReadingMode::Paged || m_vPages.isEmpty())
            return;
        QPainter oP(viewport());
        int nInset = int(m_pDocument->documentMargin());
        oP.fillRect(QRect(0, 0, viewport()->width(), nInset), m_oPaper);
        int nEnd =
            qBound(nInset, m_vPages[m_nPage].m_nEnd - m_vPages[m_nPage].m_nY, viewport()->height());
        oP.fillRect(QRect(0, nEnd, viewport()->width(), viewport()->height() - nEnd), m_oPaper);
    }
    void ReadingView::wheelEvent(QWheelEvent* pEvent)
    {
        if (m_eMode == ReadingMode::Paged)
        {
            int nDelta = pEvent->angleDelta().y();
            if (nDelta && !turnPage(nDelta < 0 ? 1 : -1))
                emit chapterBoundaryRequested(nDelta < 0 ? 1 : -1);
            pEvent->accept();
            return;
        }
        QTextBrowser::wheelEvent(pEvent);
    }
    void ReadingView::keyPressEvent(QKeyEvent* pEvent)
    {
        if (m_eMode == ReadingMode::Paged &&
            (pEvent->key() == Qt::Key_PageDown || pEvent->key() == Qt::Key_Right ||
             pEvent->key() == Qt::Key_PageUp || pEvent->key() == Qt::Key_Left))
        {
            int nDirection =
                pEvent->key() == Qt::Key_PageDown || pEvent->key() == Qt::Key_Right ? 1 : -1;
            if (!turnPage(nDirection))
                emit chapterBoundaryRequested(nDirection);
            pEvent->accept();
            return;
        }
        QTextBrowser::keyPressEvent(pEvent);
    }
    void ReadingView::hideEvent(QHideEvent* pEvent)
    {
        cancelMotion();
        m_oWarmTimer.stop();
        m_vWarmBands.clear();
        m_oImageRefreshTimer.stop();
        if (m_bImagesPending)
        {
            rebuild(readingPosition());
        }
        QTextBrowser::hideEvent(pEvent);
    }
    void ReadingView::showEvent(QShowEvent* pEvent)
    {
        QTextBrowser::showEvent(pEvent);
        if (m_bLayoutPending)
        {
            rebuild(m_nPendingPosition);
        }
        queueWarmBands();
    }
} // namespace Wenku8::Reader
