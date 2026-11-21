// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "PageControls.h"
#include "AnimatedStack.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTimer>
namespace Wenku8::Reader
{
    using namespace PageControls;
    void ClientWindow::showMobileReaderTools()
    {
        const bool bMobile = mobileMode();
        QVBoxLayout* pLayout;
        auto pPage = scrollPage(pLayout);
        pPage->setProperty("touchUi", bMobile);
        pPage->setObjectName("mobileReadingSettings");
        pLayout->setSpacing(14);
        auto* pPreviewLayout =
            settingsGroup(pLayout, "排版预览", "设置自动保存，返回即可继续阅读。");
        auto* pModeLayout = settingsGroup(pLayout, "阅读方式");
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
            pChoice->setProperty("settingsChoice", true);
            pChoice->setProperty("toolbarAction", true);
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
            paragraph(bMobile ? "轻点正文中间可开关工具栏。滚动时上下滑动，翻页时可轻点两侧或滑动。"
                              : "滚动模式使用滚轮；翻页模式支持滚轮、方向键和 PageUp／PageDown。",
                      "settingsHint"));
        auto pVolume = new QCheckBox("音量键翻页");
        pVolume->setObjectName("volumePageKeys");
        pVolume->setChecked(m_bVolumePaging);
        pVolume->setMinimumHeight(48);
        pVolume->setVisible(bMobile);
        pModeLayout->addWidget(pVolume);
        auto* pVolumeHint =
            paragraph("音量上键翻前页，下键翻后页；听书时仍调节系统音量。", "settingsHint");
        pVolumeHint->setVisible(bMobile);
        pModeLayout->addWidget(pVolumeHint);
        auto* pKeepAwake = new QCheckBox("阅读时保持屏幕常亮");
        pKeepAwake->setObjectName("readerKeepAwake");
        pKeepAwake->setMinimumHeight(48);
        pKeepAwake->setChecked(m_bKeepAwake);
        pKeepAwake->setVisible(bMobile);
        pModeLayout->addWidget(pKeepAwake);
        connect(pKeepAwake, &QCheckBox::toggled, pPage,
                [this](bool bEnabled)
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
        auto* pFontLayout = settingsGroup(pLayout, "字体与字号");
        auto* pFamily = new QFontComboBox;
        pFamily->setEditable(false);
        pFamily->setObjectName("readerFontFamily");
        pFamily->setAccessibleName("正文字体");
        pFamily->setMinimumHeight(48);
        pFamily->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pFamily->setCurrentFont(m_pReader->fontFamily().isEmpty() ? QApplication::font()
                                                                  : QFont(m_pReader->fontFamily()));
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
        pPreview->setFixedHeight(bMobile ? 152 : 164);
        pPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pPreview->setTextInteractionFlags(Qt::NoTextInteraction);
        pPreview->setFocusPolicy(Qt::NoFocus);
        auto fnPreview = [this, pPreview]
        {
            QFont oFont = m_pReader->fontFamily().isEmpty() ? QApplication::font()
                                                            : QFont(m_pReader->fontFamily());
            oFont.setPointSize(m_nFontSize);
            oFont.setBold(m_pReader->bold());
            pPreview->document()->setDefaultFont(oFont);
            pPreview->setPlainText("一页，一段旅程。\n阅读，自有节奏。");
            pPreview->document()->setDocumentMargin(
                m_pReader->sideMargin() > 0 ? m_pReader->sideMargin() : 12);
            QTextCursor oTextCursor(pPreview->document());
            oTextCursor.select(QTextCursor::Document);
            QTextCharFormat oTextFormat;
            oTextFormat.setFont(oFont);
            oTextCursor.mergeCharFormat(oTextFormat);
            for (auto oBlock = pPreview->document()->begin(); oBlock.isValid();
                 oBlock = oBlock.next())
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
        pSize->setFixedWidth(52);
        pSize->setProperty("settingsValue", true);
        auto pFonts = new QHBoxLayout;
        pFonts->setSpacing(6);
        pFonts->addWidget(paragraph("字号", "controlLabel"), 1);
        auto* pFontStepper = new QWidget;
        pFontStepper->setProperty("settingsStepper", true);
        pFontStepper->setAttribute(Qt::WA_StyledBackground);
        auto* pFontSteps = new QHBoxLayout(pFontStepper);
        pFontSteps->setContentsMargins(2, 2, 2, 2);
        pFontSteps->setSpacing(0);
        pFonts->addWidget(pFontStepper);
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
            pB->setProperty("settingsStepAction", true);
            pB->setFixedWidth(48);
            pFontSteps->addWidget(pB);
            if (nDelta < 0)
            {
                pFontSteps->addWidget(pSize);
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
        pPreviewLayout->addWidget(pPreview);
        (new TouchScroll(pPreview))->setEnabled(bMobile);
        fnPreview();
        auto* pTypographyLayout = settingsGroup(pLayout, "文字排版");
        auto fnSaveTypography = [this, fnPreview]
        {
            m_oSettings.setValue("readerLineHeight", m_pReader->lineHeightPercent());
            m_oSettings.setValue("readerParagraphSpacing", m_pReader->paragraphSpacing());
            m_oSettings.setValue("readerSideMargin", m_pReader->sideMargin());
            fnPreview();
        };
        QVector<std::function<void()>> vTypographyUpdates;
        auto fnAddTypography =
            [this, pPage, pTypographyLayout, fnSaveTypography, &vTypographyUpdates](
                const QString& strTitle, const QString& strName, int nMinimum, int nMaximum,
                int nStep, std::function<int()> fnValue, std::function<void(int)> fnApply)
        {
            auto pRow = new QHBoxLayout;
            pRow->setSpacing(6);
            pRow->addWidget(paragraph(strTitle, "controlLabel"), 1);
            auto pValue = paragraph("");
            pValue->setObjectName(strName + "Value");
            pValue->setAlignment(Qt::AlignCenter);
            pValue->setFixedWidth(52);
            pValue->setWordWrap(false);
            pValue->setProperty("settingsValue", true);
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
                pButton->setProperty("settingsStepAction", true);
                pButton->setFixedWidth(48);
            }
            auto* pStepper = new QWidget;
            pStepper->setProperty("settingsStepper", true);
            pStepper->setAttribute(Qt::WA_StyledBackground);
            auto* pSteps = new QHBoxLayout(pStepper);
            pSteps->setContentsMargins(2, 2, 2, 2);
            pSteps->setSpacing(0);
            pSteps->addWidget(pLess);
            pSteps->addWidget(pValue);
            pSteps->addWidget(pMore);
            pRow->addWidget(pStepper);
            pTypographyLayout->addLayout(pRow);
            auto fnEffectiveValue = [this, strName, fnValue]
            {
                const int nValue = fnValue();
                return strName == "readerSideMargin" && nValue == 0
                           ? (m_pReader->viewport()->width() < 600 ? 16 : 24)
                           : nValue;
            };
            auto fnRefresh =
                [pValue, pLess, pMore, strName, nMinimum, nMaximum, fnValue, fnEffectiveValue]
            {
                const int nValue = fnValue();
                const QString strValue =
                    strName == "readerSideMargin" && nValue == 0 ? QString("自动")
                    : strName == "readerParagraphSpacing" && nValue == 0
                        ? QString("紧凑")
                        : QString::number(nValue) + (strName == "readerLineHeight" ? "%" : "px");
                pValue->setText(strValue);
                pLess->setEnabled(fnEffectiveValue() > nMinimum);
                pMore->setEnabled(fnEffectiveValue() < nMaximum);
            };
            for (int nDirection : {-1, 1})
            {
                connect(nDirection < 0 ? pLess : pMore, &QPushButton::clicked, pPage,
                        [fnEffectiveValue, fnApply, fnRefresh, fnSaveTypography, nDirection,
                         nMinimum, nMaximum, nStep]
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
        fnAddTypography(
            "行距", "readerLineHeight", 120, 220, 10,
            [this]
            {
                return m_pReader->lineHeightPercent();
            },
            [this](int nValue)
            {
                m_pReader->setTypography(nValue, m_pReader->paragraphSpacing(),
                                         m_pReader->sideMargin());
            });
        fnAddTypography(
            "段间距", "readerParagraphSpacing", 0, 24, 2,
            [this]
            {
                return m_pReader->paragraphSpacing();
            },
            [this](int nValue)
            {
                m_pReader->setTypography(m_pReader->lineHeightPercent(), nValue,
                                         m_pReader->sideMargin());
            });
        fnAddTypography(
            "页边距", "readerSideMargin", 8, 48, 4,
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
        auto pResetTypography =
            action("恢复默认排版", "readerTypographyReset", Icon::RestoreBookmark);
        pResetTypography->setProperty("mobileRow", false);
        pResetTypography->setProperty("controlAction", true);
        pTypographyLayout->addWidget(pResetTypography);
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
        auto* pSearchActions = settingsGroup(pLayout, "阅读工具");
        auto* pSearch = action("章内查找", "openReaderSearch", Icon::Search);
        addGroupAction(pSearchActions, pSearch);
        connect(pSearch, &QPushButton::clicked, pPage,
                [this, pPage, bMobile]
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
        auto* pReadingActions = pSearchActions;
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
} // namespace Wenku8::Reader
