// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AppInfo.h"
#include "ReadAloudController.h"
#include "PageControls.h"
#include <QDialog>
#include <QDesktopServices>
#include <QUrl>
#include "AnimatedStack.h"
#include "TouchScroll.h"
#include "Icons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QTextBrowser>
#include <QListWidget>
#include <QLineEdit>
#include <QButtonGroup>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QInputMethod>
#include <QGuiApplication>
#include <QTextCursor>
#include <QCheckBox>
#include <QApplication>
#include <QSignalBlocker>
#include <memory>
namespace Wenku8::Reader
{
    using namespace PageControls;
    bool ClientWindow::mobileMode() const
    {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        return true;
#else
        return m_bCompact;
#endif
    }
    void ClientWindow::pushMobilePage(QWidget* pPage, const QString& strTitle)
    {
        if (m_pPages->currentIndex() == 3)
        {
            m_nMobileReaderOffset = m_pReader->readingPosition();
            saveProgress();
        }
        QGuiApplication::inputMethod()->hide();
        pPage->setProperty("mobileTitle", strTitle);
        pPage->setProperty("touchUi", mobileMode());
        m_vMobileHistory.append(m_pPages->currentIndex());
        int nIndex = m_pPages->addWidget(pPage);
        applyTheme(pPage, m_eTheme);
        m_pPages->setCurrentIndex(nIndex);
        adaptLayout();
    }
    void ClientWindow::popMobilePage()
    {
        if (m_vMobileHistory.isEmpty())
            return;
        QGuiApplication::inputMethod()->hide();
        auto pOld = m_pPages->currentWidget();
        int nTarget = m_vMobileHistory.takeLast();
        if (nTarget == 3)
            m_bRestoring = true;
        m_pPages->returnTo(nTarget);
        m_pPages->removeWidget(pOld);
        pOld->deleteLater();
        adaptLayout();
        if (nTarget == 3)
        {
            const auto nGeneration = ++m_nRestoreGeneration;
            QTimer::singleShot(0, this,
                               [this, nGeneration]
                               {
                                   if (nGeneration != m_nRestoreGeneration ||
                                       m_pPages->currentIndex() != 3)
                                       return;
                                   m_pReader->restorePosition(m_nMobileReaderOffset);
                                   m_bRestoring = false;
                                   updateReaderFooter();
                                   saveProgress();
                               });
        }
    }
    void ClientWindow::clearMobilePages()
    {
        if (m_vMobileHistory.isEmpty())
            return;
        m_pPages->setMotionEnabled(false);
        while (!m_vMobileHistory.isEmpty())
            popMobilePage();
        m_pPages->setMotionEnabled(mobileMode());
    }
    void ClientWindow::showMobileAccount(bool bTab)
    {
        if (bTab)
        {
            clearMobilePages();
            if (m_pPages->currentIndex() != 1)
                navigate(m_nSection);
        }
        QVBoxLayout* pLayout;
        auto pPage = scrollPage(pLayout);
        pPage->setObjectName("mobileAccountPage");
        pPage->setProperty("rootTab", bTab);

        auto* pAccountGroup = settingsGroup(pLayout, "文库账号");
        auto* pAccountName = paragraph(m_pAccount->text(), "controlLabel");
        pAccountName->setObjectName("mobileProfileName");
        pAccountGroup->addWidget(pAccountName);
        auto* pAccount = action(m_pLogin->text(), "mobileSignIn", Icon::SignIn);
        addGroupAction(pAccountGroup, pAccount);
        connect(pAccount, &QPushButton::clicked, this,
                [this]
                {
                    clearMobilePages();
                    m_pLogin->click();
                });

        auto* pReadingGroup = settingsGroup(pLayout, "阅读与存储");
        auto pAppearance = action("外观", "openAppearance", Icon::Sun);
        addGroupAction(pReadingGroup, pAppearance);
        connect(pAppearance, &QPushButton::clicked, this,
                [this]
                {
                    showMobileAppearance();
                });
        auto* pDownloads = action("下载与存储", "mobileDownloadSettings", Icon::Folder);
        addGroupAction(pReadingGroup, pDownloads);
        connect(pDownloads, &QPushButton::clicked, this, &ClientWindow::showDownloadSettings);
        auto pHistory = action("阅读历史", "mobileHistory", Icon::History);
        addGroupAction(pReadingGroup, pHistory);
        connect(pHistory, &QPushButton::clicked, this,
                [this]
                {
                    clearMobilePages();
                    navigate(3);
                });
        auto* pInformationGroup = settingsGroup(pLayout, "关于轻小说文库");
        const QStringList vTitles{"版本说明", "更新日志", "开源许可"};
        const char* vNames[] = {"mobileVersion", "mobileChangelog", "mobileLicenses"};
        for (int nI = 0; nI < vTitles.size(); ++nI)
        {
            auto pInfo = action(vTitles[nI], vNames[nI],
                                nI == 0   ? Icon::Profile
                                : nI == 1 ? Icon::History
                                          : Icon::Shield);
            addGroupAction(pInformationGroup, pInfo);
            connect(pInfo, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        showAppInformation(nI);
                    });
        }
        auto pFeedback = action("意见反馈", "mobileFeedback", Icon::Discover);
        addGroupAction(pInformationGroup, pFeedback);
        connect(pFeedback, &QPushButton::clicked, pPage,
                []
                {
                    QDesktopServices::openUrl(applicationFeedbackUrl());
                });
        auto pRepository = action("开源项目", "mobileRepository", Icon::Book);
        addGroupAction(pInformationGroup, pRepository);
        connect(pRepository, &QPushButton::clicked, pPage,
                []
                {
                    QDesktopServices::openUrl(applicationRepositoryUrl());
                });
        pLayout->addStretch();
        pushMobilePage(pPage, "我的");
    }
    void ClientWindow::showMobileAppearance()
    {
        QVBoxLayout* pLayout;
        auto pPage = scrollPage(pLayout);
        pPage->setObjectName("mobileAppearancePage");
        pLayout->addWidget(paragraph("让故事更适合此刻", "bookTitle"));
        pLayout->addWidget(paragraph("轻点选择，立即应用。返回后继续阅读。"));
        auto pGroup = new QButtonGroup(pPage);
        pGroup->setExclusive(true);
        const QStringList vNames{"明亮\n清晰的浅色界面", "暗色\n适合低光环境",
                                 "护眼\n温润的纸色背景"};
        for (int nI = 0; nI < 3; ++nI)
        {
            auto pChoice = action(vNames[nI], "themeChoice",
                                  nI == 0   ? Icon::Sun
                                  : nI == 1 ? Icon::Moon
                                            : Icon::Leaf);
            pChoice->setCheckable(true);
            pChoice->setChecked(int(m_eTheme) == nI);
            pChoice->setProperty("themeIndex", nI);
            pChoice->setMinimumHeight(80);
            pGroup->addButton(pChoice, nI);
            pLayout->addWidget(pChoice);
            connect(pChoice, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        setTheme(Theme(nI));
                    });
        }
        auto pSample = paragraph(
            "雨停之后的图书馆\n\n窗沿上的最后一滴水落进花盆。翻开书页，下一段故事就在这里。",
            "intro");
        pSample->setMinimumHeight(140);
        pLayout->addWidget(pSample);
        pLayout->addStretch();
        pushMobilePage(pPage, "外观");
    }
    void ClientWindow::showMobileText(const QString& strTitle, const QString& strText)
    {
        auto pPage = new QWidget;
        auto pLayout = new QVBoxLayout(pPage);
        pLayout->setContentsMargins(12, 12, 12, 16);
        auto pBrowser = new QTextBrowser;
        pBrowser->setObjectName("mobileText");
        pBrowser->setPlainText(strText);
        pBrowser->setOpenLinks(false);
        pBrowser->setTextInteractionFlags(Qt::NoTextInteraction);
        pLayout->addWidget(pBrowser);
        (new TouchScroll(pBrowser))->setEnabled(true);
        pPage->setObjectName("mobileTextPage");
        pushMobilePage(pPage, strTitle);
    }
    void ClientWindow::showInformationDocument(const QString& strTitle, const QString& strContents)
    {
        if (mobileMode())
        {
            showMobileText(strTitle, strContents);
            return;
        }
        QDialog oDialog(this);
        oDialog.setObjectName("informationDocument");
        oDialog.setWindowTitle(strTitle);
        auto pLayout = new QVBoxLayout(&oDialog);
        pLayout->setContentsMargins(24, 20, 24, 20);
        pLayout->addWidget(paragraph(strTitle, "bookTitle"));
        auto pDocument = new QTextBrowser;
        pDocument->setPlainText(strContents);
        pDocument->setOpenLinks(false);
        pLayout->addWidget(pDocument, 1);
        auto pClose = new QPushButton("关闭");
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        pLayout->addWidget(pClose, 0, Qt::AlignRight);
        applyTheme(&oDialog, m_eTheme);
        oDialog.resize(qMin(760, width() - 40), qMin(680, height() - 40));
        oDialog.exec();
    }
    void ClientWindow::showAppInformation(int nSection)
    {
        if (m_bLoadingBusy || nSection < 0 || nSection > 2)
            return;
        const auto strTitle = QStringList{"版本说明", "更新日志", "开源许可"}[nSection];
        QVBoxLayout* pLayout = nullptr;
        auto pPage = nSection == 2 ? scrollPage(pLayout) : new QWidget;
        if (nSection != 2)
        {
            pLayout = new QVBoxLayout(pPage);
            pLayout->setContentsMargins(16, 16, 16, 16);
        }
        pPage->setObjectName("appInformationPage");
        if (nSection == 2)
        {
            pLayout->addWidget(paragraph("开源许可", "bookTitle"));
            pLayout->addWidget(paragraph(
                "本应用采用 GPL-2.0-only。感谢以下开源项目；各组件的版权和许可证按原文保留。"));
            auto pOverview = action("版权与许可说明", "licenseOverview", Icon::Book);
            pLayout->addWidget(pOverview);
            connect(pOverview, &QPushButton::clicked, this,
                    [this]
                    {
                        showInformationDocument("版权与许可说明", licenseOverview());
                    });
            if (!mobileMode())
            {
                auto pComplete = action("完整第三方版权声明\nQt WebEngine、Chromium 与全部随附组件",
                                        "desktopCompleteNotices", Icon::List);
                pLayout->addWidget(pComplete);
                connect(pComplete, &QPushButton::clicked, this,
                        [this]
                        {
                            showDesktopNotices();
                        });
            }
            for (const auto& oEntry : applicationLicenses())
            {
                auto pComponent = action(oEntry.m_strTitle + "\n" + oEntry.m_strLicense,
                                         "licenseComponent", Icon::Book);
                pLayout->addWidget(pComponent);
                connect(pComponent, &QPushButton::clicked, this,
                        [this, oEntry]
                        {
                            showInformationDocument(oEntry.m_strTitle,
                                                    licenseText(oEntry.m_strResource));
                        });
            }
            pLayout->addStretch();
        }
        else
        {
            auto pDocument = new QTextBrowser;
            pDocument->setObjectName("informationText");
            pDocument->setFrameShape(QFrame::NoFrame);
            pDocument->setOpenLinks(false);
            pDocument->setOpenExternalLinks(false);
            pDocument->document()->setDefaultStyleSheet(
                QString("body{line-height:1.6;}h2{font-size:23px;}p{margin-top:12px;margin-bottom:"
                        "12px;}a{color:%1;}")
                    .arg(colors(m_eTheme).m_oSelectionText.name()));
            pDocument->setHtml(nSection == 0 ? versionDescription() : releaseNotes());
            pLayout->addWidget(pDocument, 1);
            connect(pDocument, &QTextBrowser::anchorClicked, pPage,
                    [](const QUrl& oUrl)
                    {
                        if (isApplicationLink(oUrl))
                        {
                            QDesktopServices::openUrl(oUrl);
                        }
                    });
            if (mobileMode())
            {
                pDocument->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
                (new TouchScroll(pDocument))->setEnabled(true);
            }
        }
        if (nSection == 0)
        {
            auto pLinks = new QHBoxLayout;
            auto pFeedback = action("意见反馈", "informationFeedback", Icon::Discover);
            auto pRepository = action("开源项目", "informationRepository", Icon::Book);
            pFeedback->setProperty("textAction", true);
            pRepository->setProperty("textAction", true);
            pFeedback->setProperty("mobileRow", false);
            pRepository->setProperty("mobileRow", false);
            pLinks->addWidget(pFeedback);
            pLinks->addWidget(pRepository);
            pLayout->addLayout(pLinks);
            connect(pFeedback, &QPushButton::clicked, pPage,
                    []
                    {
                        QDesktopServices::openUrl(applicationFeedbackUrl());
                    });
            connect(pRepository, &QPushButton::clicked, pPage,
                    []
                    {
                        QDesktopServices::openUrl(applicationRepositoryUrl());
                    });
        }
        if (mobileMode())
        {
            pushMobilePage(pPage, strTitle);
            return;
        }
        QDialog oDialog(this);
        oDialog.setObjectName("appInformationDialog");
        oDialog.setWindowTitle(strTitle);
        auto pDialogLayout = new QVBoxLayout(&oDialog);
        pDialogLayout->setContentsMargins(20, 16, 20, 16);
        pDialogLayout->addWidget(pPage, 1);
        auto pClose = new QPushButton("关闭");
        pClose->setMinimumHeight(40);
        pDialogLayout->addWidget(pClose, 0, Qt::AlignRight);
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        applyTheme(&oDialog, m_eTheme);
        oDialog.resize(qMin(680, width() - 40), qMin(640, height() - 40));
        oDialog.exec();
    }
    void ClientWindow::showMobileBookmarks()
    {
        auto pPage = new QWidget;
        pPage->setObjectName("mobileBookmarksPage");
        auto pLayout = new QVBoxLayout(pPage);
        pLayout->setContentsMargins(4, 8, 4, 8);
        pLayout->setSpacing(12);
        auto* pAdd = action("添加当前位置", "mobileBookmarkAddCurrent", Icon::Bookmark);
        pAdd->setProperty("mobileRow", false);
        pAdd->setProperty("controlAction", true);
        pAdd->setEnabled(m_bUnlocked && !m_bLoadingBusy && m_nChapter >= 0 &&
                         m_nChapter < m_vFlat.size());
        pLayout->addWidget(pAdd);
        connect(pAdd, &QPushButton::clicked, pPage,
                [this]
                {
                    if (!m_bUnlocked || m_bLoadingBusy || m_nChapter < 0 ||
                        m_nChapter >= m_vFlat.size())
                    {
                        return;
                    }
                    const int nBook = m_oBook.m_nId;
                    const int nChapter = m_nChapter;
                    while (!m_vMobileHistory.isEmpty() && m_pPages->currentIndex() >= 4)
                    {
                        popMobilePage();
                    }
                    const auto nGeneration = m_nRestoreGeneration;
                    QTimer::singleShot(0, this,
                                       [this, nBook, nChapter, nGeneration]
                                       {
                                           if (m_pPages->currentIndex() != 3 ||
                                               m_oBook.m_nId != nBook || m_nChapter != nChapter ||
                                               m_nRestoreGeneration != nGeneration)
                                           {
                                               return;
                                           }
                                           // A zero timer may run before the queued pop restore.
                                           // Finish that restore before recording the bookmark.
                                           if (m_bRestoring)
                                           {
                                               ++m_nRestoreGeneration;
                                               m_pReader->restorePosition(m_nMobileReaderOffset);
                                               m_bRestoring = false;
                                           }
                                           addBookmark();
                                           showMobileBookmarks();
                                       });
                });
        const int nBook = m_oBook.m_nId;
        auto* pManage = action("管理", "mobileBookmarkManage", Icon::Settings);
        pManage->setProperty("mobileRow", false);
        pManage->setProperty("controlAction", true);
        pManage->setCheckable(true);
        pLayout->addWidget(pManage, 0, Qt::AlignRight);
        auto* pHint = paragraph("轻点书签继续阅读；管理模式中可以选择和删除。");
        pHint->setObjectName("mobileBookmarkHint");
        pLayout->addWidget(pHint);
        auto* pEmpty = paragraph("还没有书签，点击上方「添加当前位置」保存当前阅读位置。");
        pEmpty->setObjectName("mobileBookmarkEmpty");
        pLayout->addWidget(pEmpty);
        auto* pList = new QListWidget;
        pList->setObjectName("mobileBookmarkItems");
        pList->setWordWrap(true);
        pList->setSelectionMode(QAbstractItemView::NoSelection);
        pList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pList->setAccessibleName("轻点继续阅读的书签列表");
        pLayout->addWidget(pList, 1);
        auto* pRemove = action("删除选中书签", "mobileRemoveBookmark", Icon::Cancel);
        pRemove->setProperty("mobileRow", false);
        pRemove->setProperty("controlAction", true);
        pRemove->setEnabled(false);
        pRemove->hide();
        pLayout->addWidget(pRemove);
        auto* pUndo = action("已删除书签 · 撤销删除", "mobileUndoBookmark", Icon::RestoreBookmark);
        pUndo->setProperty("mobileRow", false);
        pUndo->setProperty("controlAction", true);
        pUndo->hide();
        pLayout->addWidget(pUndo);
        auto* pUndoTimer = new QTimer(pPage);
        pUndoTimer->setSingleShot(true);
        pUndoTimer->setInterval(5000);
        auto pEntries = std::make_shared<QVector<BookmarkEntry>>();
        auto pDeleted = std::make_shared<std::optional<BookmarkEntry>>();
        auto fnReload = [this, nBook, pEntries, pList, pEmpty, pManage, pRemove]
        {
            *pEntries = m_pReadingState->bookmarks(nBook);
            pList->clear();
            for (const auto& oEntry : *pEntries)
            {
                auto* pItem = new QListWidgetItem(oEntry.m_strTitle + "\n" + oEntry.m_strSnippet,
                                                 pList);
                pItem->setSizeHint(QSize(200, 96));
                pItem->setToolTip(oEntry.m_strTitle);
            }
            pList->setCurrentRow(-1);
            pRemove->setEnabled(false);
            pEmpty->setVisible(pEntries->isEmpty());
            pManage->setEnabled(!pEntries->isEmpty());
            if (pEntries->isEmpty())
            {
                pManage->setChecked(false);
            }
        };
        connect(pManage, &QPushButton::toggled, pPage,
                [pList, pHint, pManage, pRemove](bool bManaging)
                {
                    pManage->setText(bManaging ? "完成" : "管理");
                    pList->setSelectionMode(bManaging ? QAbstractItemView::SingleSelection
                                                      : QAbstractItemView::NoSelection);
                    pList->setCurrentRow(-1);
                    pList->clearSelection();
                    pRemove->setEnabled(false);
                    pRemove->setVisible(bManaging);
                    pHint->setText(bManaging ? "选择要删除的书签。删除后 5 秒内可撤销。"
                                             : "轻点书签继续阅读；管理模式中可以选择和删除。");
                });
        connect(pList, &QListWidget::itemSelectionChanged, pPage,
                [pList, pManage, pRemove]
                {
                    pRemove->setEnabled(pManage->isChecked() && !pList->selectedItems().isEmpty());
                });
        auto fnActivate = [this, pPage, nBook, pEntries, pManage, pList, pHint](QListWidgetItem* pItem)
        {
            if (!pItem || m_pPages->currentWidget() != pPage || m_oBook.m_nId != nBook ||
                m_bLoadingBusy)
            {
                return;
            }
            if (pManage->isChecked())
            {
                pList->setCurrentItem(pItem);
                pItem->setSelected(true);
                return;
            }
            const int nRow = pList->row(pItem);
            if (nRow < 0 || nRow >= pEntries->size())
            {
                return;
            }
            const BookmarkEntry oEntry = pEntries->at(nRow);
            int nTarget = -1;
            for (int nIndex = 0; nIndex < m_vFlat.size(); ++nIndex)
            {
                if (m_vFlat[nIndex].m_nId == oEntry.m_nChapter)
                {
                    nTarget = nIndex;
                    break;
                }
            }
            if (nTarget < 0)
            {
                pHint->setText("这条书签的章节已不在当前目录中。");
                return;
            }
            const bool bSameChapter = nTarget == m_nChapter;
            if (m_pReadAloud)
            {
                m_pReadAloud->setFollow(false);
            }
            m_nMobileReaderOffset = oEntry.m_nOffset;
            while (!m_vMobileHistory.isEmpty() && m_pPages->currentIndex() >= 4)
            {
                popMobilePage();
            }
            if (!bSameChapter)
            {
                m_nRestoreOffset = oEntry.m_nOffset;
                openChapter(nTarget);
            }
        };
        connect(pList, &QListWidget::itemClicked, pPage, fnActivate);
        connect(pList, &QListWidget::itemActivated, pPage, fnActivate);
        (new TouchScroll(pList,
                         [pList, fnActivate](QPoint oPoint)
                         {
                             fnActivate(pList->itemAt(oPoint));
                         }))
            ->setEnabled(true);
        connect(pRemove, &QPushButton::clicked, pPage,
                [this, nBook, pEntries, pDeleted, pList, pManage, pUndo, pUndoTimer, fnReload]
                {
                    const int nRow = pList->currentRow();
                    if (!pManage->isChecked() || pList->selectedItems().isEmpty() || nRow < 0 ||
                        nRow >= pEntries->size() || m_oBook.m_nId != nBook)
                    {
                        return;
                    }
                    *pDeleted = pEntries->at(nRow);
                    m_pReadingState->removeBookmark(nBook, pDeleted->value().m_strId);
                    fnReload();
                    pUndo->show();
                    pUndoTimer->start();
                });
        connect(pUndo, &QPushButton::clicked, pPage,
                [this, nBook, pDeleted, pUndo, pUndoTimer, fnReload]
                {
                    if (!pDeleted->has_value() || m_oBook.m_nId != nBook)
                    {
                        return;
                    }
                    m_pReadingState->addBookmark(nBook, pDeleted->value());
                    pDeleted->reset();
                    pUndoTimer->stop();
                    pUndo->hide();
                    fnReload();
                });
        connect(pUndoTimer, &QTimer::timeout, pPage,
                [pDeleted, pUndo]
                {
                    pDeleted->reset();
                    pUndo->hide();
                });
        fnReload();
        pushMobilePage(pPage, "本书书签");
    }
} // namespace Wenku8::Reader
