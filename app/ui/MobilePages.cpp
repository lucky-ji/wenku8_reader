// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AppInfo.h"
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
#include <QFontComboBox>
#include <QApplication>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
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
        pLayout->addWidget(paragraph(m_pAccount->text(), "bookTitle"));
        pLayout->addWidget(paragraph("你的文库账号与阅读偏好。"));
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("阅读与存储", "sectionTitle"));
        auto* pReadingGroup = controlGroup(pLayout);
        auto pAppearance = action("外观\n明亮、暗色与护眼", "openAppearance", Icon::Sun);
        addGroupAction(pReadingGroup, pAppearance);
        connect(pAppearance, &QPushButton::clicked, this,
                [this]
                {
                    showMobileAppearance();
                });
        auto* pDownloads = action("下载与存储\n缓存与本地保存位置", "mobileDownloadSettings", Icon::Folder);
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
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("账号", "sectionTitle"));
        auto* pAccountGroup = controlGroup(pLayout);
        auto pAccount = action(m_pLogin->text(), "mobileSignIn", Icon::SignIn);
        addGroupAction(pAccountGroup, pAccount);
        connect(pAccount, &QPushButton::clicked, this,
                [this]
                {
                    clearMobilePages();
                    m_pLogin->click();
                });
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("关于轻小说文库", "sectionTitle"));
        auto* pInformationGroup = controlGroup(pLayout);
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
        auto pFeedback = action("意见反馈\n在 GitHub 提交问题与建议", "mobileFeedback", Icon::Discover);
        addGroupAction(pInformationGroup, pFeedback);
        connect(pFeedback, &QPushButton::clicked, pPage,
                []
                {
                    QDesktopServices::openUrl(applicationFeedbackUrl());
                });
        auto pRepository = action("开源项目\n在 GitHub 查看源码", "mobileRepository", Icon::Book);
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
    void ClientWindow::showMobileReaderTools()
    {
        const bool bMobile = mobileMode();
        QVBoxLayout* pLayout;
        auto pPage = scrollPage(pLayout);
        pPage->setProperty("touchUi", bMobile);
        pPage->setObjectName("mobileReadingSettings");
        pLayout->addWidget(paragraph("阅读方式", "sectionTitle"));
        auto* pModeLayout = controlGroup(pLayout);
        auto pModes = new QHBoxLayout;
        pModes->setSpacing(6);
        auto pGroup = new QButtonGroup(pPage);
        pGroup->setExclusive(true);
        for (int nI = 0; nI < 2; ++nI)
        {
            auto pChoice = action(nI == 0 ? "滚动阅读" : "翻页阅读", "readingModeChoice",
                                  nI == 0 ? Icon::List : Icon::Book);
            pChoice->setCheckable(true);
            pChoice->setProperty("mobileRow", false);
            pChoice->setProperty("controlAction", true);
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
        pModeLayout->addLayout(pModes);
        pModeLayout->addWidget(
            paragraph("滚动模式上下滑动；翻页模式左右轻点或滑动翻页，轻点中间显示工具栏。"));
        auto pVolume = new QCheckBox("音量键翻页");
        pVolume->setObjectName("volumePageKeys");
        pVolume->setChecked(m_bVolumePaging);
        pVolume->setMinimumHeight(48);
        pVolume->setVisible(bMobile);
        pModeLayout->addWidget(pVolume);
        auto* pVolumeHint = paragraph("音量上键：上一页 · 音量下键：下一页");
        pVolumeHint->setVisible(bMobile);
        pModeLayout->addWidget(pVolumeHint);
        auto* pKeepAwake = new QCheckBox("阅读时保持屏幕常亮");
        pKeepAwake->setObjectName("readerKeepAwake");
        pKeepAwake->setMinimumHeight(48);
        pKeepAwake->setChecked(m_bKeepAwake);
        pKeepAwake->setVisible(bMobile);
        pModeLayout->addWidget(pKeepAwake);
        connect(pKeepAwake, &QCheckBox::toggled, pPage, [this](bool bEnabled)
        {
            m_bKeepAwake = bEnabled;
            saveReaderPreferences();
            updateVolumePaging();
        });
        connect(pVolume, &QCheckBox::toggled, pPage,
                [this](bool bEnabled)
                {
                    m_bVolumePaging = bEnabled;
                    saveReaderPreferences();
                    updateVolumePaging();
                });
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("文字大小", "sectionTitle"));
        auto* pFontLayout = controlGroup(pLayout);
        auto* pFamily = new QFontComboBox;
        pFamily->setEditable(false);
        pFamily->setObjectName("readerFontFamily");
        pFamily->setAccessibleName("正文字体");
        pFamily->setMinimumHeight(48);
        pFamily->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pFamily->setCurrentFont(m_pReader->fontFamily().isEmpty()
                                   ? QApplication::font() : QFont(m_pReader->fontFamily()));
        pFontLayout->addWidget(paragraph("正文字体", "controlLabel"));
        pFontLayout->addWidget(pFamily);
        auto* pFontOptions = new QHBoxLayout;
        auto* pBold = new QCheckBox("正文粗体");
        pBold->setObjectName("readerBold");
        pBold->setMinimumHeight(48);
        pBold->setChecked(m_pReader->bold());
        pFontOptions->addWidget(pBold, 1);
        auto* pDefaultFont = action("系统默认", "readerDefaultFont", Icon::RestoreBookmark);
        pDefaultFont->setProperty("mobileRow", false);
        pDefaultFont->setProperty("textAction", true);
        pFontOptions->addWidget(pDefaultFont);
        pFontLayout->addLayout(pFontOptions);
        auto* pPreview = new QTextBrowser;
        pPreview->setObjectName("readerTypographyPreview");
        pPreview->setAccessibleName("即时排版预览");
        pPreview->setFrameShape(QFrame::NoFrame);
        pPreview->setFixedHeight(180);
        pPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pPreview->setTextInteractionFlags(Qt::NoTextInteraction);
        pPreview->setFocusPolicy(Qt::NoFocus);
        auto fnPreview = [this, pPreview]
        {
            QFont oFont = m_pReader->fontFamily().isEmpty()
                              ? QApplication::font() : QFont(m_pReader->fontFamily());
            oFont.setPointSize(m_nFontSize);
            oFont.setBold(m_pReader->bold());
            pPreview->document()->setDefaultFont(oFont);
            pPreview->setPlainText("雨停之后，窗沿的水珠悄然落下。翻开书页，下一段故事就在这里。\n"
                                   "在这里预览字号、字体和排版，设置会自动保存。");
            pPreview->document()->setDocumentMargin(m_pReader->sideMargin() > 0
                                                        ? m_pReader->sideMargin() : 12);
            QTextCursor oTextCursor(pPreview->document());
            oTextCursor.select(QTextCursor::Document);
            QTextCharFormat oTextFormat;
            oTextFormat.setFont(oFont);
            oTextCursor.mergeCharFormat(oTextFormat);
            for (auto oBlock = pPreview->document()->begin(); oBlock.isValid(); oBlock = oBlock.next())
            {
                QTextCursor oCursor(oBlock);
                QTextBlockFormat oFormat = oCursor.blockFormat();
                oFormat.setLineHeight(m_pReader->lineHeightPercent(),
                                      QTextBlockFormat::ProportionalHeight);
                oFormat.setBottomMargin(m_pReader->paragraphSpacing());
                oCursor.setBlockFormat(oFormat);
            }
            pPreview->moveCursor(QTextCursor::Start);
        };
        auto fnSaveFont = [this, fnPreview]
        {
            m_oSettings.setValue("readerFontFamily", m_pReader->fontFamily());
            m_oSettings.setValue("readerBold", m_pReader->bold());
            fnPreview();
        };
        connect(pFamily, &QFontComboBox::currentFontChanged, pPage,
                [this, fnSaveFont](const QFont& oFont)
                {
                    m_pReader->setReadingFont(oFont.family(), m_pReader->bold());
                    fnSaveFont();
                });
        connect(pBold, &QCheckBox::toggled, pPage,
                [this, fnSaveFont](bool bBold)
                {
                    m_pReader->setReadingFont(m_pReader->fontFamily(), bBold);
                    fnSaveFont();
                });
        connect(pDefaultFont, &QPushButton::clicked, pPage,
                [this, pFamily, fnSaveFont]
                {
                    m_pReader->setReadingFont({}, m_pReader->bold());
                    const QSignalBlocker oBlocker(pFamily);
                    pFamily->setCurrentFont(QApplication::font());
                    fnSaveFont();
                });
        auto pSize = new QLabel(QString::number(m_nFontSize));
        pSize->setObjectName("mobileFontSize");
        pSize->setAlignment(Qt::AlignCenter);
        pSize->setMinimumWidth(52);
        auto pFonts = new QHBoxLayout;
        pFonts->setSpacing(6);
        pFonts->addWidget(paragraph("字号", "controlLabel"), 1);
        for (int nDelta : {-1, 1})
        {
            auto pB = action(nDelta < 0 ? "缩小文字" : "放大文字",
                             nDelta < 0 ? "mobileFontSmaller" : "mobileFontLarger",
                             nDelta < 0 ? Icon::TextSmaller : Icon::TextLarger);
            pB->setText(nDelta < 0 ? "A−" : "A＋");
            pB->setAccessibleName(nDelta < 0 ? "缩小文字" : "放大文字");
            pB->setIcon(QIcon());
            pB->setProperty("wenkuIcon", QVariant());
            pB->setProperty("mobileRow", false);
            pB->setProperty("controlAction", true);
            pB->setFixedWidth(52);
            pFonts->addWidget(pB);
            if (nDelta < 0)
            {
                pFonts->addWidget(pSize);
            }
            connect(pB, &QPushButton::clicked, pPage,
                    [this, pSize, nDelta, fnPreview]
                    {
                        m_nFontSize = qBound(10, m_nFontSize + nDelta, 36);
                        m_pReader->appearance(m_nFontSize, m_eTheme);
                        pSize->setText(QString::number(m_nFontSize));
                        m_oSettings.setValue("fontSize", m_nFontSize);
                        fnPreview();
                    });
        }
        pFontLayout->addLayout(pFonts);
        pFontLayout->addWidget(paragraph("即时预览", "sectionTitle"));
        pFontLayout->addWidget(pPreview);
        (new TouchScroll(pPreview))->setEnabled(bMobile);
        fnPreview();
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("文字排版", "sectionTitle"));
        auto* pTypographyLayout = controlGroup(pLayout);
        auto fnSaveTypography = [this, fnPreview]
        {
            m_oSettings.setValue("readerLineHeight", m_pReader->lineHeightPercent());
            m_oSettings.setValue("readerParagraphSpacing", m_pReader->paragraphSpacing());
            m_oSettings.setValue("readerSideMargin", m_pReader->sideMargin());
            fnPreview();
        };
        QVector<std::function<void()>> vTypographyUpdates;
        auto fnAddTypography = [this, pPage, pTypographyLayout, fnSaveTypography, &vTypographyUpdates](
                                   const QString& strTitle, const QString& strName,
                                   int nMinimum, int nMaximum, int nStep,
                                   std::function<int()> fnValue,
                                   std::function<void(int)> fnApply)
        {
            auto pRow = new QHBoxLayout;
            pRow->setSpacing(6);
            pRow->addWidget(paragraph(strTitle, "controlLabel"), 1);
            auto pValue = paragraph("");
            pValue->setObjectName(strName + "Value");
            pValue->setAlignment(Qt::AlignCenter);
            pValue->setMinimumWidth(52);
            auto pLess = action("更紧凑", "typographyDecrease", Icon::TextSmaller);
            auto pMore = action("更宽松", "typographyIncrease", Icon::TextLarger);
            pLess->setObjectName(strName + "Decrease");
            pMore->setObjectName(strName + "Increase");
            pLess->setAccessibleName(strTitle + "更紧凑");
            pMore->setAccessibleName(strTitle + "更宽松");
            for (auto* pButton : {pLess, pMore})
            {
                pButton->setText(pButton == pLess ? "−" : "＋");
                pButton->setIcon(QIcon());
                pButton->setProperty("wenkuIcon", QVariant());
                pButton->setProperty("mobileRow", false);
                pButton->setProperty("controlAction", true);
                pButton->setFixedWidth(52);
            }
            pRow->addWidget(pLess);
            pRow->addWidget(pValue);
            pRow->addWidget(pMore);
            pTypographyLayout->addLayout(pRow);
            auto fnEffectiveValue = [this, strName, fnValue]
            {
                const int nValue = fnValue();
                return strName == "readerSideMargin" && nValue == 0
                           ? (m_pReader->viewport()->width() < 600 ? 16 : 24)
                           : nValue;
            };
            auto fnRefresh = [pValue, pLess, pMore, strName, nMinimum, nMaximum,
                              fnValue, fnEffectiveValue]
            {
                const int nValue = fnValue();
                const QString strValue = strName == "readerSideMargin" && nValue == 0
                                             ? QString("自动")
                                         : strName == "readerParagraphSpacing" && nValue == 0
                                             ? QString("紧凑")
                                             : QString::number(nValue) +
                                                   (strName == "readerLineHeight" ? "%" : "px");
                pValue->setText(strValue);
                pLess->setEnabled(fnEffectiveValue() > nMinimum);
                pMore->setEnabled(fnEffectiveValue() < nMaximum);
            };
            for (int nDirection : {-1, 1})
            {
                connect(nDirection < 0 ? pLess : pMore, &QPushButton::clicked, pPage,
                        [fnEffectiveValue, fnApply, fnRefresh, fnSaveTypography,
                         nDirection, nMinimum, nMaximum, nStep]
                        {
                            fnApply(qBound(nMinimum, fnEffectiveValue() + nDirection * nStep,
                                           nMaximum));
                            fnRefresh();
                            fnSaveTypography();
                        });
            }
            vTypographyUpdates.append(fnRefresh);
            fnRefresh();
        };
        fnAddTypography("行距", "readerLineHeight", 120, 220, 10,
                        [this]
                        {
                            return m_pReader->lineHeightPercent();
                        },
                        [this](int nValue)
                        {
                            m_pReader->setTypography(nValue, m_pReader->paragraphSpacing(),
                                                     m_pReader->sideMargin());
                        });
        fnAddTypography("段间距", "readerParagraphSpacing", 0, 24, 2,
                        [this]
                        {
                            return m_pReader->paragraphSpacing();
                        },
                        [this](int nValue)
                        {
                            m_pReader->setTypography(m_pReader->lineHeightPercent(), nValue,
                                                     m_pReader->sideMargin());
                        });
        fnAddTypography("页边距", "readerSideMargin", 8, 48, 4,
                        [this]
                        {
                            return m_pReader->sideMargin();
                        },
                        [this](int nValue)
                        {
                            m_pReader->setTypography(m_pReader->lineHeightPercent(),
                                                     m_pReader->paragraphSpacing(), nValue);
                        });
        if (!bMobile)
        {
            auto* pWidth = new QComboBox;
            pWidth->setObjectName("readerComfortWidth");
            pWidth->setAccessibleName("桌面舒适阅读宽度");
            pWidth->addItem("全宽", 0);
            for (int nWidth : {720, 880, 1040})
            {
                pWidth->addItem(QString("%1 像素").arg(nWidth), nWidth);
            }
            pWidth->setCurrentIndex(qMax(0, pWidth->findData(m_pReader->comfortWidth())));
            auto* pWidthRow = new QHBoxLayout;
            pWidthRow->addWidget(paragraph("舒适宽度", "controlLabel"), 1);
            pWidthRow->addWidget(pWidth);
            pTypographyLayout->addLayout(pWidthRow);
            connect(pWidth, QOverload<int>::of(&QComboBox::currentIndexChanged), pPage,
                    [this, pWidth](int)
                    {
                        m_pReader->setComfortWidth(pWidth->currentData().toInt());
                        m_oSettings.setValue("readerComfortWidth", m_pReader->comfortWidth());
                    });
        }
        auto pResetTypography = action("恢复默认排版", "readerTypographyReset", Icon::RestoreBookmark);
        pResetTypography->setProperty("mobileRow", false);
        pResetTypography->setProperty("controlAction", true);
        pTypographyLayout->addWidget(pResetTypography);
        pLayout->addWidget(paragraph("排版设置自动保存，阅读位置保持不变。"));
        connect(pResetTypography, &QPushButton::clicked, pPage,
                [this, fnSaveTypography, vTypographyUpdates]
                {
                    m_pReader->setTypography(145, 8, 0);
                    fnSaveTypography();
                    for (const auto& fnRefresh : vTypographyUpdates)
                    {
                        fnRefresh();
                    }
                });
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("正文查找", "sectionTitle"));
        auto* pSearchActions = controlGroup(pLayout);
        auto* pSearch = action("章内查找", "openReaderSearch", Icon::Search);
        addGroupAction(pSearchActions, pSearch);
        connect(pSearch, &QPushButton::clicked, pPage, [this, pPage, bMobile]
        {
            if (!bMobile)
            {
                if (auto* pDialog = qobject_cast<QDialog*>(pPage->window()))
                {
                    pDialog->accept();
                }
                QTimer::singleShot(0, this, &ClientWindow::showReaderSearch);
                return;
            }
            showReaderSearch();
        });
        if (!bMobile)
        {
            pLayout->addStretch();
            QDialog oDialog(this);
            oDialog.setObjectName("readerPreferencesDialog");
            for (auto* pControl : pPage->findChildren<QPushButton*>())
            {
                pControl->setMinimumHeight(36);
            }
            for (auto* pControl : pPage->findChildren<QComboBox*>())
            {
                pControl->setMinimumHeight(36);
            }
            for (auto* pControl : pPage->findChildren<QCheckBox*>())
            {
                pControl->setMinimumHeight(36);
            }
            oDialog.setWindowTitle("阅读设置");
            auto pDialogLayout = new QVBoxLayout(&oDialog);
            pDialogLayout->setContentsMargins(16, 16, 16, 16);
            pDialogLayout->addWidget(pPage, 1);
            auto pClose = new QPushButton("完成");
            pClose->setMinimumHeight(40);
            pDialogLayout->addWidget(pClose, 0, Qt::AlignRight);
            connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
            applyTheme(&oDialog, m_eTheme);
            oDialog.resize(qMin(720, width() - 40), qMin(680, height() - 40));
            oDialog.exec();
            return;
        }
        pLayout->addSpacing(8);
        pLayout->addWidget(paragraph("外观与书签", "sectionTitle"));
        auto* pReadingActions = controlGroup(pLayout);
        auto pAppearance = action("阅读外观", "readingAppearance", Icon::Sun);
        addGroupAction(pReadingActions, pAppearance);
        connect(pAppearance, &QPushButton::clicked, this,
                [this]
                {
                    showMobileAppearance();
                });
        auto pBookmarks = action("本书书签", "mobileBookmarkList", Icon::Bookmark);
        addGroupAction(pReadingActions, pBookmarks);
        connect(pBookmarks, &QPushButton::clicked, this,
                [this]
                {
                    showMobileBookmarks();
                });
        auto pMark = action("添加当前位置书签", "mobileAddBookmark", Icon::Bookmark);
        addGroupAction(pReadingActions, pMark);
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
