// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Reader.h"
#include "Theme.h"
#include "ReadingState.h"
#include "CoverStore.h"
#include <QMainWindow>
#include <QSettings>
#include <QSet>
#include <QPointer>
class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QListWidget;
class QTreeWidget;
class QStackedWidget;
class QProgressBar;
class QCheckBox;
class QSplitter;
class QVBoxLayout;
class QHBoxLayout;
class QKeyEvent;
class QMenuBar;
namespace Wenku8::Reader
{
    class OnlineSession;
    class AnimatedStack;
    class TouchScroll;
    class LoadingPanel;
    class PullRefreshIndicator;
    class DownloadOption;
    class ClientWindow final : public QMainWindow
    {
      public:
        ClientWindow(const QString& strDirectory, const QString& strOrigin,
                     const QString& strSessionDirectory = {});
        ~ClientWindow() override;
        void start(int nNovel = 0);
        void setTheme(Theme);
        void setReadingMode(ReadingMode);

      protected:
        void closeEvent(QCloseEvent*) override;
        void resizeEvent(QResizeEvent*) override;
        void keyPressEvent(QKeyEvent*) override;
        bool eventFilter(QObject*, QEvent*) override;

      private:
        QSettings m_oSettings;
        std::unique_ptr<ReadingState> m_pReadingState;
        std::unique_ptr<CoverStore> m_pCovers;
        QString m_strDirectory, m_strOrigin;
        Theme m_eTheme = Theme::Light;
        bool m_bUnlocked = false, m_bRestoring = false, m_bLocalBook = false,
             m_bResumeOnOpen = false;
        quint64 m_nRestoreGeneration = 0;
        QSet<int> m_vShelfIds;
        bool m_bReaderFullscreen = false, m_bLoadingBusy = false;
        LoadingPanel* m_pLoading = nullptr;
        LoadingPanel* m_pStartupLoading = nullptr;
        QPointer<QWidget> m_pLoadingFocus;
        Qt::WindowStates m_oPreviousWindowState;
        QWidget* m_pHeading = nullptr;
        bool m_bCompact = false, m_bReaderToolsOpen = false;
        bool m_bReaderChrome = true;
        bool m_bVolumePaging = true;
        QVector<int> m_vMobileHistory;
        int m_nMobileReaderOffset = 0;
        TouchScroll *m_pCardsTouch = nullptr, *m_pTocTouch = nullptr, *m_pReaderTouch = nullptr;
        QLabel* m_pMobileTitle = nullptr;
        QWidget *m_pMobileHeader = nullptr, *m_pMobileNavigation = nullptr,
                *m_pReaderTools = nullptr;
        QPushButton* m_pReaderToolsToggle = nullptr;
        QList<QPushButton*> m_vMobileNav;
        QVBoxLayout *m_pBodyLayout = nullptr, *m_pDetailLayout = nullptr;
        QHBoxLayout* m_pDetailHeaderLayout = nullptr;
        int m_nSection = 0, m_nPage = 1, m_nChapter = -1, m_nPendingChapter = -1,
            m_nRestoreOffset = -1, m_nFontSize = 18, m_nStartupNovel = 0;
        std::unique_ptr<NovelDownloader> m_pStore;
        OnlineSession* m_pSession = nullptr;
        Wenku8::Novel m_oBook;
        QVector<Chapter> m_vFlat;
        QList<QPushButton*> m_vNav;
        QWidget* m_pSidebar = nullptr;
        QMenuBar* m_pDesktopMenuBar = nullptr;
        AnimatedStack* m_pPages = nullptr;
        QStackedWidget* m_pStartupPages = nullptr;
        QLabel *m_pTitle = nullptr, *m_pSubtitle = nullptr, *m_pStatus = nullptr,
               *m_pAccount = nullptr, *m_pCount = nullptr, *m_pDetailCover = nullptr,
               *m_pDetailTitle = nullptr, *m_pDetailMeta = nullptr, *m_pDetailIntro = nullptr,
               *m_pReadTitle = nullptr, *m_pChapterLabel = nullptr;
        QLineEdit* m_pSearch = nullptr;
        QPushButton *m_pRefresh = nullptr, *m_pLogin = nullptr, *m_pRead = nullptr,
                    *m_pDownload = nullptr, *m_pShelfAdd = nullptr, *m_pFullscreen = nullptr,
                    *m_pPrevious = nullptr, *m_pNext = nullptr, *m_pPreviousPage = nullptr,
                    *m_pNextPage = nullptr;
        QPushButton* m_pReaderContents = nullptr;
        QComboBox* m_pThemes = nullptr;
        DownloadOption* m_pIllustrations = nullptr;
        PullRefreshIndicator *m_pCardsRefresh = nullptr, *m_pTocRefresh = nullptr;
        QListWidget* m_pCards = nullptr;
        QTreeWidget* m_pToc = nullptr;
        ReadingView* m_pReader = nullptr;
        QWidget* m_pOperations = nullptr;
        QWidget* m_pStatusArea = nullptr;
        QPushButton *m_pRetryNetwork = nullptr, *m_pVerifyNetwork = nullptr;
        bool m_bVerificationNeeded = false;
        quint64 m_nBusinessContext = 0, m_nActiveRequestContext = 0, m_nRetryContext = 0;
        QString m_strActiveSearch;
        void build();
        void adaptLayout();
        void showAccountSheet();
        bool mobileMode() const;
        void pushMobilePage(QWidget*, const QString&);
        void popMobilePage();
        void clearMobilePages();
        void showMobileAccount(bool bTab = false);
        void showMobileAppearance();
        void showMobileReaderTools();
        void showReaderContents();
        void saveReaderPreferences();
        void updateReaderFooter();
        void turnReaderPage(int nDirection);
        void updateVolumePaging();
        void showMobileBookmarks();
        void showMobileText(const QString&, const QString&);
        void showAppInformation(int nSection);
        void showDesktopNotices();
        void showInformationDocument(const QString& strTitle, const QString& strContents);
        void goBack();
        void authenticate(bool);
        void navigate(int);
        void refresh(bool bForce = false);
        void refreshCurrentPage();
        void positionPullIndicators();
        void populate(const std::vector<Novel>&);
        void downloadedBooks();
        void openBook(int, bool bResume = false);
        void historyBooks();
        void requestCover(int);
        void showBookmarks();
        void addBookmark();
        void highlightChapter();
        void updateShelfAction();
        void toggleReaderFullscreen();
        void leaveReaderFullscreen();
        void showBook(const Novel&, const std::vector<Volume>&, bool bLocal);
        void openChapter(int);
        void renderChapter(const QString&);
        QString progressKey() const;
        void saveProgress(bool bVisited = false);
        void updateBusy(bool);
        void updateNetworkActions();
        void message(const QString&);
    };
} // namespace Wenku8::Reader
