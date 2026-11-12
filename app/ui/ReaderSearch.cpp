// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "PageControls.h"
#include "AnimatedStack.h"
#include <QDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QGuiApplication>
#include <QTextCursor>
namespace Wenku8::Reader
{
    using namespace PageControls;
    void ClientWindow::showReaderSearch()
    {
        if (!m_bUnlocked || m_bLoadingBusy || m_nChapter < 0 || m_nChapter >= m_vFlat.size())
        {
            return;
        }
        const bool bMobile = mobileMode();
        QVBoxLayout* pLayout = nullptr;
        auto* pPage = scrollPage(pLayout);
        pPage->setObjectName("readerSearchPage");
        pPage->setProperty("touchUi", bMobile);
        pLayout->addWidget(paragraph(QString::fromUtf8(m_vFlat[m_nChapter].m_strTitle.c_str()),
                                     "sectionTitle"));
        auto* pSearchGroup = controlGroup(pLayout);
        auto* pSearch = new QLineEdit;
        pSearch->setObjectName("readerSearchInput");
        pSearch->setAccessibleName("章内查找关键词");
        pSearch->setPlaceholderText("输入本章要查找的文字");
        pSearch->setMinimumHeight(48);
        pSearch->setClearButtonEnabled(true);
        pSearchGroup->addWidget(pSearch);
        auto* pActions = new QHBoxLayout;
        auto* pPrevious = action("上一处", "readerSearchPrevious", Icon::Left);
        auto* pNext = action("下一处", "readerSearchNext", Icon::Right);
        for (auto* pButton : {pPrevious, pNext})
        {
            pButton->setProperty("mobileRow", false);
            pButton->setProperty("controlAction", true);
            pButton->setEnabled(false);
            pActions->addWidget(pButton, 1);
        }
        pSearchGroup->addLayout(pActions);
        auto* pStatus = paragraph("输入关键词后，点击上一处或下一处查找。搜索范围仅为当前章节。");
        pStatus->setObjectName("readerSearchStatus");
        pLayout->addWidget(pStatus);
        auto* pPreviewGroup = controlGroup(pLayout);
        auto* pPreview = paragraph("找到匹配后，将在这里显示附近的正文。", "intro");
        pPreview->setObjectName("readerSearchPreview");
        pPreview->setMinimumHeight(96);
        pPreviewGroup->addWidget(pPreview);
        auto* pRead = action("阅读此处", "readerSearchRead", Icon::Book);
        pRead->setProperty("mobileRow", false);
        pRead->setProperty("primary", true);
        pRead->setEnabled(false);
        pLayout->addWidget(pRead);
        pLayout->addStretch();

        auto fnSearch = [this, pPage, pSearch, pStatus, pPreview, pRead, bMobile](bool bBackward)
        {
            const QString strQuery = pSearch->text().trimmed();
            if (strQuery.isEmpty())
            {
                return;
            }
            QGuiApplication::inputMethod()->hide();
            if (!m_pReader->findInChapter(strQuery, bBackward))
            {
                pStatus->setText("本章没有找到匹配的文字，请修改关键词后重试。");
                pPreview->setText("没有匹配结果。");
                pRead->setEnabled(false);
                pPage->setProperty("searchMatchOffset", QVariant());
                return;
            }
            const int nOffset = m_pReader->textCursor().selectionStart();
            pPage->setProperty("searchMatchOffset", nOffset);
            pRead->setEnabled(true);
            pStatus->setText("已找到匹配。可继续查找，或阅读此处；查找会在本章首尾循环。");
            pPreview->setText(m_pReader->readingSnippet(qMax(0, nOffset - 40), 220));
            if (bMobile)
            {
                // popMobilePage restores this anchor after the reader becomes visible.
                // Keep it in sync instead of restoring the position before search.
                m_nMobileReaderOffset = nOffset;
            }
        };
        connect(pSearch, &QLineEdit::textChanged, pPage,
                [pPage, pSearch, pPrevious, pNext, pRead, pStatus, pPreview]
                {
                    const bool bHasQuery = !pSearch->text().trimmed().isEmpty();
                    pPrevious->setEnabled(bHasQuery);
                    pNext->setEnabled(bHasQuery);
                    pRead->setEnabled(false);
                    pPage->setProperty("searchMatchOffset", QVariant());
                    pStatus->setText("点击上一处或下一处查找，搜索范围仅为当前章节。");
                    pPreview->setText("找到匹配后，将在这里显示附近的正文。");
                });
        connect(pPrevious, &QPushButton::clicked, pPage,
                [fnSearch]
                {
                    fnSearch(true);
                });
        connect(pNext, &QPushButton::clicked, pPage,
                [fnSearch]
                {
                    fnSearch(false);
                });
        connect(pSearch, &QLineEdit::returnPressed, pPage,
                [fnSearch]
                {
                    fnSearch(false);
                });
        if (bMobile)
        {
            connect(pRead, &QPushButton::clicked, pPage,
                    [this, pPage]
                    {
                        const QVariant oOffset = pPage->property("searchMatchOffset");
                        if (!oOffset.isValid())
                        {
                            return;
                        }
                        m_nMobileReaderOffset = oOffset.toInt();
                        while (!m_vMobileHistory.isEmpty() && m_pPages->currentIndex() >= 4)
                        {
                            popMobilePage();
                        }
                    });
            pushMobilePage(pPage, "章内查找");
            return;
        }
        QDialog oDialog(this);
        oDialog.setObjectName("readerSearchDialog");
        oDialog.setWindowTitle("章内查找");
        auto* pDialogLayout = new QVBoxLayout(&oDialog);
        pDialogLayout->setContentsMargins(16, 16, 16, 16);
        pDialogLayout->addWidget(pPage);
        auto* pClose = new QPushButton("关闭");
        pClose->setMinimumHeight(40);
        pDialogLayout->addWidget(pClose, 0, Qt::AlignRight);
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        connect(pRead, &QPushButton::clicked, &oDialog, &QDialog::accept);
        applyTheme(&oDialog, m_eTheme);
        oDialog.resize(qMin(600, width() - 40), qMin(520, height() - 40));
        oDialog.exec();
    }
}
