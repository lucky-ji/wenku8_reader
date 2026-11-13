// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "PageControls.h"
#include "AnimatedStack.h"
#include <QTextDocument>
#include <QRegularExpression>
#include <memory>
#include <algorithm>
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
        pPreview->setTextFormat(Qt::RichText);
        pPreviewGroup->addWidget(pPreview);
        auto* pRead = action("阅读此处", "readerSearchRead", Icon::Book);
        pRead->setProperty("mobileRow", false);
        pRead->setProperty("primary", true);
        pRead->setEnabled(false);
        pLayout->addWidget(pRead);
        pLayout->addStretch();

        auto pMatches = std::make_shared<QVector<int>>();
        auto fnSearch = [this, pPage, pSearch, pStatus, pPreview, pRead, bMobile, pMatches](bool bBackward)
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
            if (pPage->property("countedQuery").toString() != strQuery)
            {
                pMatches->clear();
                QTextCursor oCursor(m_pReader->document());
                while (pMatches->size() < 10000)
                {
                    oCursor = m_pReader->document()->find(strQuery, oCursor);
                    if (oCursor.isNull())
                    {
                        break;
                    }
                    pMatches->append(oCursor.selectionStart());
                }
                pPage->setProperty("countedQuery", strQuery);
            }
            const int nOffset = m_pReader->textCursor().selectionStart();
            pPage->setProperty("searchMatchOffset", nOffset);
            pRead->setEnabled(true);
            const auto oFound = std::lower_bound(pMatches->cbegin(), pMatches->cend(), nOffset);
            const int nOrdinal = int(oFound - pMatches->cbegin()) + 1;
            const bool bLimited = pMatches->size() >= 10000;
            pStatus->setText(bLimited ? "当前匹配 · 至少 10000 处，支持首尾循环"
                                     : QString("第 %1 / %2 处 · 支持首尾循环").arg(nOrdinal).arg(pMatches->size()));
            const QString strSnippet = m_pReader->readingSnippet(qMax(0, nOffset - 40), 220);
            const QRegularExpression oPattern(QRegularExpression::escape(strQuery),
                                              QRegularExpression::CaseInsensitiveOption);
            auto oMatches = oPattern.globalMatch(strSnippet);
            QString strHtml;
            int nPrevious = 0;
            const auto oColors = colors(m_eTheme);
            while (oMatches.hasNext())
            {
                const auto oMatch = oMatches.next();
                strHtml += strSnippet.mid(nPrevious, oMatch.capturedStart() - nPrevious).toHtmlEscaped();
                strHtml += QString("<span style='background-color:%1;color:%2;font-weight:600'>%3</span>")
                               .arg(oColors.m_oSelection.name(), oColors.m_oSelectionText.name(),
                                    oMatch.captured().toHtmlEscaped());
                nPrevious = oMatch.capturedEnd();
            }
            strHtml += strSnippet.mid(nPrevious).toHtmlEscaped();
            pPreview->setText(strHtml);
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
                    pPage->setProperty("countedQuery", QVariant());
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
        pRead->setText("定位到正文");
        connect(pRead, &QPushButton::clicked, this, [this]
        {
            m_pReader->setFocus();
        });
        showReaderPanel(pPage, "章内查找");
        pSearch->setFocus();
    }
}
