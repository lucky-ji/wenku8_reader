// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "DesktopNotices.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <QSplitter>
#include <QDesktopServices>
#include <QUrl>
namespace Wenku8::Reader
{
    void ClientWindow::showDesktopNotices()
    {
        if (m_bLoadingBusy || mobileMode())
            return;
        QDialog oDialog(this);
        oDialog.setObjectName("desktopThirdPartyNotices");
        oDialog.setWindowTitle("完整第三方版权声明");
        auto pLayout = new QVBoxLayout(&oDialog);
        pLayout->setContentsMargins(24, 20, 24, 20);
        pLayout->setSpacing(12);
        auto pTitle = new QLabel("完整第三方版权声明");
        pTitle->setObjectName("bookTitle");
        pLayout->addWidget(pTitle);
        auto pDescription = new QLabel(
            "浏览随桌面版本提供的原始版权和许可证。可按组件或文件名称搜索，正文支持选择与复制。");
        pDescription->setObjectName("muted");
        pDescription->setWordWrap(true);
        pLayout->addWidget(pDescription);
        auto pSearch = new QLineEdit;
        pSearch->setObjectName("desktopNoticeSearch");
        pSearch->setPlaceholderText("搜索组件或许可文件，例如 Chromium、Qt、LICENSE");
        pSearch->setClearButtonEnabled(true);
        pLayout->addWidget(pSearch);
        auto pSplitter = new QSplitter;
        auto pFiles = new QListWidget;
        pFiles->setObjectName("desktopNoticeFiles");
        pFiles->setWordWrap(true);
        auto pText = new QTextBrowser;
        pText->setObjectName("desktopNoticeText");
        pText->setOpenLinks(false);
        pSplitter->addWidget(pFiles);
        pSplitter->addWidget(pText);
        pSplitter->setStretchFactor(0, 1);
        pSplitter->setStretchFactor(1, 2);
        pLayout->addWidget(pSplitter, 1);
        const auto strDirectory = desktopNoticeDirectory();
        const auto vDocuments = desktopNotices(strDirectory);
        for (const auto& oDocument : vDocuments)
        {
            auto pItem = new QListWidgetItem(oDocument.m_strTitle, pFiles);
            pItem->setData(Qt::UserRole, oDocument.m_strFile);
            pItem->setToolTip(oDocument.m_strTitle);
        }
        connect(pFiles, &QListWidget::currentItemChanged, &oDialog,
                [strDirectory, pText](QListWidgetItem* pCurrent, QListWidgetItem*)
                {
                    if (pCurrent)
                        pText->setPlainText(desktopNoticeText(
                            strDirectory, pCurrent->data(Qt::UserRole).toString()));
                    else
                        pText->setPlainText("没有匹配的版权声明。");
                });
        connect(pSearch, &QLineEdit::textChanged, &oDialog,
                [pFiles](const QString& strValue)
                {
                    QListWidgetItem* pFirst = nullptr;
                    for (int nI = 0; nI < pFiles->count(); ++nI)
                    {
                        auto pItem = pFiles->item(nI);
                        const bool bMatch =
                            pItem->text().contains(strValue.trimmed(), Qt::CaseInsensitive);
                        pItem->setHidden(!bMatch);
                        if (bMatch && !pFirst)
                            pFirst = pItem;
                    }
                    pFiles->setCurrentItem(pFirst);
                });
        if (vDocuments.isEmpty())
        {
            pText->setPlainText(
                "此运行目录未附带完整的第三方版权文件。请使用完整安装包，或查看发布包中的许可证及对"
                "应源码。应用内「开源许可」仍可查看项目与主要组件的许可正文。");
            pSearch->setEnabled(false);
        }
        else
            pFiles->setCurrentRow(0);
        auto pButtons = new QHBoxLayout;
        auto pDirectoryButton = new QPushButton("打开许可文件夹");
        pDirectoryButton->setEnabled(!strDirectory.isEmpty());
        pButtons->addWidget(pDirectoryButton);
        pButtons->addStretch();
        auto pClose = new QPushButton("关闭");
        pButtons->addWidget(pClose);
        pLayout->addLayout(pButtons);
        connect(pDirectoryButton, &QPushButton::clicked, &oDialog,
                [strDirectory]
                {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(strDirectory));
                });
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        applyTheme(&oDialog, m_eTheme);
        oDialog.resize(qMin(1080, width() - 40), qMin(720, height() - 40));
        oDialog.exec();
    }
} // namespace Wenku8::Reader
