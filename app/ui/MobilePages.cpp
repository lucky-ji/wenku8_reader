// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AppInfo.h"
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
#include <limits>
#include "AndroidKeys.h"
namespace Wenku8::Reader
{
    namespace
    {
        QPushButton* action(const QString& strTitle, const char* pName, Icon eIcon)
        {
            auto pB = new QPushButton(strTitle);
            pB->setObjectName(pName);
            pB->setMinimumSize(48, 56);
            pB->setProperty("mobileRow", true);
            pB->setCursor(Qt::PointingHandCursor);
            setButtonIcon(pB, eIcon);
            return pB;
        }
        QLabel* paragraph(const QString& strValue, const char* pName = "muted")
        {
            auto pL = new QLabel(strValue);
            pL->setObjectName(pName);
            pL->setTextFormat(Qt::PlainText);
            pL->setWordWrap(true);
            return pL;
        }
        QWidget* scrollPage(QVBoxLayout*& pContent)
        {
            auto pPage = new QWidget;
            pPage->setProperty("touchUi", true);
            auto pOuter = new QVBoxLayout(pPage);
            pOuter->setContentsMargins(0, 0, 0, 0);
            auto pScroll = new QScrollArea;
            pScroll->setFrameShape(QFrame::NoFrame);
            pScroll->setWidgetResizable(true);
            pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto pBody = new QWidget;
            pContent = new QVBoxLayout(pBody);
            pContent->setContentsMargins(8, 12, 8, 16);
            pContent->setSpacing(14);
            pScroll->setWidget(pBody);
            pOuter->addWidget(pScroll);
            (new TouchScroll(pScroll))->setEnabled(true);
            return pPage;
        }
    } // namespace
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
        pLayout->addWidget(paragraph(m_pAccount->text(), "bookTitle"));
        pLayout->addWidget(paragraph("你的文库账号与阅读偏好。"));
        auto pAppearance = action("外观\n明亮、暗色与护眼", "openAppearance", Icon::Sun);
        pLayout->addWidget(pAppearance);
        connect(pAppearance, &QPushButton::clicked, this,
                [this]
                {
                    showMobileAppearance();
                });
        auto pHistory = action("阅读历史", "mobileHistory", Icon::History);
        pLayout->addWidget(pHistory);
        connect(pHistory, &QPushButton::clicked, this,
                [this]
                {
                    clearMobilePages();
                    navigate(3);
                });
        auto pAccount = action(m_pLogin->text(), "mobileSignIn", Icon::SignIn);
        pLayout->addWidget(pAccount);
        connect(pAccount, &QPushButton::clicked, this,
                [this]
                {
                    clearMobilePages();
                    m_pLogin->click();
                });
        const QStringList vTitles{"版本说明", "更新日志", "开源许可"};
        const char* vNames[] = {"mobileVersion", "mobileChangelog", "mobileLicenses"};
        for (int nI = 0; nI < vTitles.size(); ++nI)
        {
            auto pInfo = action(vTitles[nI], vNames[nI],
                                nI == 0   ? Icon::Profile
                                : nI == 1 ? Icon::History
                                          : Icon::Shield);
            pLayout->addWidget(pInfo);
            connect(pInfo, &QPushButton::clicked, this,
                    [this, nI]
                    {
                        showAppInformation(nI);
                    });
        }
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
        pLayout->setContentsMargins(4, 8, 4, 8);
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
                        if (oUrl == QUrl("https://github.com/lucky-ji"))
                            QDesktopServices::openUrl(oUrl);
                    });
            if (mobileMode())
            {
                pDocument->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
                (new TouchScroll(pDocument))->setEnabled(true);
            }
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
    void ClientWindow::showMobileReaderTools()
    {
        QVBoxLayout* pLayout;
        auto pPage = scrollPage(pLayout);
        pPage->setObjectName("mobileReadingSettings");
        pLayout->addWidget(paragraph("阅读偏好", "bookTitle"));
        pLayout->addWidget(
            paragraph("滚动模式上下滑动；翻页模式左右轻点或滑动翻页，轻点中间显示工具栏。"));
        auto pModes = new QHBoxLayout;
        auto pGroup = new QButtonGroup(pPage);
        pGroup->setExclusive(true);
        for (int nI = 0; nI < 2; ++nI)
        {
            auto pChoice = action(nI == 0 ? "滚动阅读" : "翻页阅读", "readingModeChoice",
                                  nI == 0 ? Icon::List : Icon::Book);
            pChoice->setCheckable(true);
            pChoice->setProperty("modeIndex", nI);
            pChoice->setChecked(int(m_pReader->readingMode()) == nI);
            pGroup->addButton(pChoice, nI);
            pModes->addWidget(pChoice);
            connect(pChoice, &QPushButton::clicked, pPage,
                    [this, nI]
                    {
                        setReadingMode(ReadingMode(nI));
                    });
        }
        pLayout->addLayout(pModes);
        auto pVolume = new QCheckBox("音量键翻页（上键上一页，下键下一页）");
        pVolume->setObjectName("volumePageKeys");
        pVolume->setChecked(m_bVolumePaging);
        pVolume->setMinimumHeight(48);
        pLayout->addWidget(pVolume);
        connect(pVolume, &QCheckBox::toggled, pPage,
                [this](bool bEnabled)
                {
                    m_bVolumePaging = bEnabled;
                    saveReaderPreferences();
                    updateVolumePaging();
                });
        auto pSize = new QLabel(QString("字号  %1").arg(m_nFontSize));
        pSize->setObjectName("mobileFontSize");
        pLayout->addWidget(pSize);
        auto pFonts = new QHBoxLayout;
        for (int nDelta : {-1, 1})
        {
            auto pB = action(nDelta < 0 ? "缩小文字" : "放大文字",
                             nDelta < 0 ? "mobileFontSmaller" : "mobileFontLarger",
                             nDelta < 0 ? Icon::TextSmaller : Icon::TextLarger);
            pFonts->addWidget(pB);
            connect(pB, &QPushButton::clicked, pPage,
                    [this, pSize, nDelta]
                    {
                        m_nFontSize = qBound(10, m_nFontSize + nDelta, 36);
                        m_pReader->appearance(m_nFontSize, m_eTheme);
                        pSize->setText(QString("字号  %1").arg(m_nFontSize));
                        m_oSettings.setValue("fontSize", m_nFontSize);
                    });
        }
        pLayout->addLayout(pFonts);
        auto pAppearance = action("阅读外观", "readingAppearance", Icon::Sun);
        pLayout->addWidget(pAppearance);
        connect(pAppearance, &QPushButton::clicked, this,
                [this]
                {
                    showMobileAppearance();
                });
        auto pBookmarks = action("本书书签", "mobileBookmarkList", Icon::Bookmark);
        pLayout->addWidget(pBookmarks);
        connect(pBookmarks, &QPushButton::clicked, this,
                [this]
                {
                    showMobileBookmarks();
                });
        auto pMark = action("添加当前位置书签", "mobileAddBookmark", Icon::Bookmark);
        pLayout->addWidget(pMark);
        connect(pMark, &QPushButton::clicked, this,
                [this]
                {
                    popMobilePage();
                    QTimer::singleShot(0, this,
                                       [this]
                                       {
                                           addBookmark();
                                       });
                });
        auto pSearch = new QLineEdit;
        pSearch->setObjectName("mobileChapterSearch");
        pSearch->setPlaceholderText("在当前章节查找");
        pSearch->setMinimumHeight(48);
        pSearch->setClearButtonEnabled(true);
        pLayout->addWidget(pSearch);
        auto pFind = action("查找正文", "mobileFind", Icon::Search);
        pLayout->addWidget(pFind);
        auto fnPerform = [this, pSearch]
        {
            const auto strQuery = pSearch->text().trimmed();
            if (strQuery.isEmpty())
                return;
            popMobilePage();
            QTimer::singleShot(0, this,
                               [this, strQuery]
                               {
                                   if (!m_pReader->findInChapter(strQuery))
                                       message("本章没有找到这段文字。");
                               });
        };
        connect(pFind, &QPushButton::clicked, pPage, fnPerform);
        connect(pSearch, &QLineEdit::returnPressed, pPage, fnPerform);
        pLayout->addStretch();
        pushMobilePage(pPage, "阅读设置");
    }
    void ClientWindow::showMobileBookmarks()
    {
        auto pPage = new QWidget;
        pPage->setObjectName("mobileBookmarksPage");
        auto pLayout = new QVBoxLayout(pPage);
        pLayout->setContentsMargins(4, 8, 4, 8);
        pLayout->setSpacing(12);
        auto vEntries = m_pReadingState->bookmarks(m_oBook.m_nId);
        if (vEntries.isEmpty())
        {
            pLayout->addWidget(paragraph("还没有书签。在阅读设置中添加当前位置。"));
            pLayout->addStretch();
        }
        else
        {
            auto pList = new QListWidget;
            pList->setObjectName("mobileBookmarkItems");
            pList->setWordWrap(true);
            pList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            pLayout->addWidget(pList, 1);
            for (const auto& oEntry : vEntries)
            {
                auto pItem =
                    new QListWidgetItem(oEntry.m_strTitle + "\n" + oEntry.m_strSnippet, pList);
                pItem->setSizeHint(QSize(200, 96));
            }
            auto pJump = action("跳转到书签", "mobileJumpBookmark", Icon::Book);
            pLayout->addWidget(pJump);
            connect(pJump, &QPushButton::clicked, pPage,
                    [this, pList, vEntries]
                    {
                        int nRow = pList->currentRow();
                        if (nRow < 0 || nRow >= vEntries.size())
                            return;
                        const auto oEntry = vEntries[nRow];
                        while (m_pPages->currentIndex() >= 4)
                            popMobilePage();
                        for (int nI = 0; nI < m_vFlat.size(); ++nI)
                            if (m_vFlat[nI].m_nId == oEntry.m_nChapter)
                            {
                                m_nRestoreOffset = oEntry.m_nOffset;
                                openChapter(nI);
                                return;
                            }
                    });
            (new TouchScroll(pList,
                             [pList](QPoint oPoint)
                             {
                                 if (auto pItem = pList->itemAt(oPoint))
                                     pList->setCurrentItem(pItem);
                             }))
                ->setEnabled(true);
            auto pRemove = action("删除选中书签", "mobileRemoveBookmark", Icon::Cancel);
            pLayout->addWidget(pRemove);
            connect(pRemove, &QPushButton::clicked, pPage,
                    [this, pList, vEntries]
                    {
                        int nRow = pList->currentRow();
                        if (nRow >= 0 && nRow < vEntries.size())
                        {
                            m_pReadingState->removeBookmark(m_oBook.m_nId, vEntries[nRow].m_strId);
                            popMobilePage();
                            showMobileBookmarks();
                        }
                    });
        }
        pushMobilePage(pPage, "本书书签");
    }
    void ClientWindow::setReadingMode(ReadingMode eMode)
    {
        m_pReader->setReadingMode(eMode);
        m_pReaderTouch->setPageGestures(eMode == ReadingMode::Paged,
                                        [this](int nDirection)
                                        {
                                            turnReaderPage(nDirection);
                                        });
        for (auto pChoice : findChildren<QPushButton*>("readingModeChoice"))
            pChoice->setChecked(pChoice->property("modeIndex").toInt() == int(eMode));
        saveReaderPreferences();
        updateReaderFooter();
    }
    void ClientWindow::saveReaderPreferences()
    {
#ifndef Q_OS_ANDROID
#endif
        m_oSettings.setValue("readingMode", int(m_pReader->readingMode()));
        m_oSettings.setValue("volumePageKeys", m_bVolumePaging);
    }
    void ClientWindow::updateReaderFooter()
    {
        if (m_nChapter < 0)
            return;
        const bool bPaged = m_pReader->readingMode() == ReadingMode::Paged;
        m_pChapterLabel->setText(
            bPaged ? QString("%1/%2 章 · %3/%4 页")
                         .arg(m_nChapter + 1)
                         .arg(m_vFlat.size())
                         .arg(m_pReader->pageNumber())
                         .arg(m_pReader->pageCount())
                   : QString("%1 / %2 章").arg(m_nChapter + 1).arg(m_vFlat.size()));
        m_pPrevious->setText(bPaged ? (mobileMode() ? "上页" : "上一页") : "上一章");
        m_pNext->setText(bPaged ? (mobileMode() ? "下页" : "下一页") : "下一章");
        m_pPrevious->setEnabled(m_pRead->isEnabled() &&
                                (m_nChapter > 0 || (bPaged && m_pReader->pageNumber() > 1)));
        m_pNext->setEnabled(m_pRead->isEnabled() &&
                            (m_nChapter + 1 < m_vFlat.size() ||
                             (bPaged && m_pReader->pageNumber() < m_pReader->pageCount())));
    }
    void ClientWindow::turnReaderPage(int nDirection)
    {
        if (m_bLoadingBusy)
            return;
        if (!m_bUnlocked || m_pPages->currentIndex() != 3 || m_nChapter < 0 || m_bRestoring ||
            !m_pRead->isEnabled())
            return;
        if (m_pReader->turnPage(nDirection))
            return;
        int nNext = m_nChapter + (nDirection < 0 ? -1 : 1);
        if (nNext >= 0 && nNext < m_vFlat.size())
        {
            m_nRestoreOffset = nDirection < 0 ? std::numeric_limits<int>::max() : 0;
            openChapter(nNext);
        }
        else
            message(nDirection < 0 ? "已到本书开头。" : "已到本书末尾。");
    }
    void ClientWindow::updateVolumePaging()
    {
        setAndroidVolumePaging(m_bVolumePaging && m_bUnlocked && !m_bLoadingBusy &&
                               m_pPages->currentIndex() == 3 &&
                               QGuiApplication::applicationState() == Qt::ApplicationActive);
    }
} // namespace Wenku8::Reader
