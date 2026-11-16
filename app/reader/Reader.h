// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#ifndef Q_MOC_RUN
#include "Wenku8Download.h"
#endif
#include "Theme.h"
#include <QTextBrowser>
#include <QStringList>
#include <functional>
#include <QCache>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
class QPropertyAnimation;
class QVariantAnimation;
class QPainter;
namespace Wenku8::Reader
{
    enum class ReadingMode
    {
        Scroll,
        Paged
    };
    std::filesystem::path nativePath(const QString&);
    std::unique_ptr<NovelDownloader> openStore(const QString&, const QString& strOrigin);
    struct ChapterText
    {
        QString m_strHtml;
        QStringList m_vImages;
    };
    // Input is plain UTF-8 chapter text with SDK image markers, never trusted HTML.
    ChapterText formatChapter(const QString& strTitle, const QString& strText);
    class LocalDocument final : public QTextDocument
    {
      public:
        using ImageLoader = std::function<QByteArray(const QString&)>;
        explicit LocalDocument(QObject* pParent = nullptr);
        void setChapter(const ChapterText&, ImageLoader, int nImageWidth, int nImageHeight = 0,
                        int nLineHeightPercent = 145, int nParagraphSpacing = 8);
        int loadedImages() const
        {
            return m_nLoadedImages;
        }

      protected:
        QVariant loadResource(int nType, const QUrl& oName) override;

      private:
        QStringList m_vImages;
        ImageLoader m_fnLoader;
        int m_nImageWidth = 600, m_nImageHeight = 0, m_nLoadedImages = 0;
    };
    // Logical UTF-16 document offset: stable across font, window and theme changes.
    class ReadingView final : public QTextBrowser
    {
        Q_OBJECT
      public:
        explicit ReadingView(QWidget* pParent = nullptr);
        void showChapter(const QString&, const QString&, LocalDocument::ImageLoader);
        int readingPosition() const;
        QString readingSnippet(int nPosition, int nLength = 100) const;
        void restorePosition(int);
        void appearance(int nPointSize, Theme);
        void appearance(int nPointSize, bool bDark)
        {
            appearance(nPointSize, bDark ? Theme::Dark : Theme::Light);
        }
        void setTypography(int nLineHeightPercent, int nParagraphSpacing, int nSideMargin);
        void setReadingFont(const QString& strFamily, bool bBold);
        QString fontFamily() const
        {
            return m_strReadingFontFamily;
        }
        bool bold() const
        {
            return m_bReadingBold;
        }
        void setComfortWidth(int nMaxWidth);
        int comfortWidth() const
        {
            return m_nComfortWidth;
        }
        int lineHeightPercent() const
        {
            return m_nLineHeightPercent;
        }
        int paragraphSpacing() const
        {
            return m_nParagraphSpacing;
        }
        int sideMargin() const
        {
            return m_nSideMargin;
        }
        int loadedImages() const
        {
            return m_pDocument->loadedImages();
        }
        ReadingMode readingMode() const
        {
            return m_eMode;
        }
        void setReadingMode(ReadingMode);
        int pageNumber() const
        {
            return m_nPage + 1;
        }
        int pageCount() const
        {
            return m_vPages.size();
        }
        bool turnPage(int nDirection);
        bool findInChapter(const QString&, bool bBackward = false);
        int chapterProgress() const;
        bool canSeekProgress() const;
        void seekChapterProgress(int nPermille);
        void stopMotion()
        {
            cancelMotion();
        }
        // A completed illustration batch may arrive after the text. Preserve the
        // logical anchor and wait for a gesture/animation to finish before relayout.
        void refreshImages();
        bool moving() const;
        int rasterCacheCostKiB() const
        {
            return m_vRasterBands.totalCost();
        }
      signals:
        void pageStateChanged();
        void chapterBoundaryRequested(int nDirection);

      protected:
        bool viewportEvent(QEvent*) override;
        void scrollContentsBy(int nDx, int nDy) override;
        void resizeEvent(QResizeEvent*) override;
        void paintEvent(QPaintEvent*) override;
        void wheelEvent(QWheelEvent*) override;
        void keyPressEvent(QKeyEvent*) override;
        void hideEvent(QHideEvent*) override;
        void showEvent(QShowEvent*) override;

      private:
        LocalDocument* m_pDocument;
        bool m_bRebuilding = false;
        bool m_bLayoutPending = false, m_bAppearanceSet = false;
        int m_nPendingPosition = 0;
        QSize m_oLayoutViewport;
        Theme m_eAppearanceTheme = Theme::Light;
        int m_nLineHeightPercent = 145, m_nParagraphSpacing = 8, m_nSideMargin = 0;
        QString m_strReadingFontFamily, m_strDefaultFontFamily;
        bool m_bReadingBold = false, m_bUpdatingComfortMargins = false;
        int m_nComfortWidth = 0;
        void updateComfortMargins();
        ChapterText m_oChapter;
        LocalDocument::ImageLoader m_fnLoader;
        void rebuild(int nPosition);
        void reflow(int nPosition);
        struct Page
        {
            int m_nY, m_nOffset, m_nEnd;
        };
        QVector<Page> m_vPages;
        int m_nPage = 0;
        ReadingMode m_eMode = ReadingMode::Scroll;
        int m_nWheelDelta = 0, m_nWheelDirection = 0;
        bool m_bWheelGestureActive = false, m_bWheelGestureTurned = false,
             m_bWheelPixelInput = false;
        QElapsedTimer m_oWheelEventTimer, m_oWheelTurnTimer;
        void resetWheelInput(bool bResetCooldown = true);
        QColor m_oPaper = Qt::white;
        QPropertyAnimation* m_pScrollAnimation = nullptr;
        QVariantAnimation* m_pTurnAnimation = nullptr;
        QWidget* m_pPageOverlay = nullptr;
        int m_nScrollTarget = -1;
        static constexpr int m_nBandHeight = 128;
        QCache<int, QPixmap> m_vRasterBands{32 * 1024}; // KiB, native DPR text; never downsample.
        QSize m_oRasterViewport;
        qreal m_fRasterDpr = 0;
        int m_nRasterRevision = -1, m_nWarmDirection = 1, m_nRasterSelectionStart = -1,
            m_nRasterSelectionEnd = -1;
        QTimer m_oWarmTimer, m_oImageRefreshTimer;
        QVector<int> m_vWarmBands;
        bool m_bImagesPending = false;
        void clearRasterCache();
        void validateRasterCache();
        QPixmap rasterBand(int nIndex);
        void paintBands(QPainter&, const QRect&, int nDocumentY);
        QPixmap pageSnapshot(int nIndex);
        void queueWarmBands();
        void warmNextBand();
        void paginate();
        void cancelMotion();
    };
} // namespace Wenku8::Reader
