// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "OnlineSession.h"
#include "DownloadManager.h"
#include "Toast.h"
#include "ExportDestination.h"
#include "DownloadOption.h"
#include "Icons.h"
#include "TouchScroll.h"
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Wenku8::Reader
{
    namespace
    {
        bool insideCache(const QString& strDestination, const QString& strCache)
        {
            if (strDestination.startsWith("content://"))
            {
                return false;
            }
            auto fnCanonical = [](const QString& strPath)
            {
                const QFileInfo oFile(strPath);
                const QString strCanonical = oFile.canonicalFilePath();
                return QDir::cleanPath(strCanonical.isEmpty() ? oFile.absoluteFilePath()
                                                              : strCanonical);
            };
            const QString strTarget = fnCanonical(strDestination);
            const QString strRoot = fnCanonical(strCache);
#ifdef Q_OS_WIN
            const auto eCase = Qt::CaseInsensitive;
#else
            const auto eCase = Qt::CaseSensitive;
#endif
            return strTarget.compare(strRoot, eCase) == 0 ||
                   strTarget.startsWith(strRoot + '/', eCase);
        }
    }

    void ClientWindow::cacheBook()
    {
        if (!m_bUnlocked || m_pSession->busy() || m_bLocalBook || m_oBook.m_nId <= 0)
        {
            return;
        }
        const QString strId = m_pSession->downloads()->enqueue(m_oBook.m_nId,
            QString::fromUtf8(m_oBook.m_strTitle.c_str()), m_pIllustrations->isChecked());
        if (!strId.isEmpty())
        {
            m_pToast->showText("已加入后台缓存，可在「下载缓存」查看进度", m_eTheme);
        }
    }

    void ClientWindow::chooseLocalDownloadDirectory(std::function<void(bool)> fnAfter)
    {
        const QPointer<ClientWindow> pGuard(this);
        chooseExportDirectory(this, [pGuard, fnAfter](const ExportDirectory& oDirectory)
        {
            if (!pGuard)
            {
                return;
            }
            bool bSelected = false;
            if (!oDirectory.m_strError.isEmpty())
            {
                pGuard->message(oDirectory.m_strError);
            }
            else if (!oDirectory.m_strLocation.isEmpty())
            {
                if (insideCache(oDirectory.m_strLocation, pGuard->m_strDirectory))
                {
                    pGuard->message("请选择应用缓存之外的目录，分别管理缓存与本地文件。");
                }
                else
                {
                    pGuard->m_oSettings.setValue("localDownloadDirectory", oDirectory.m_strLocation);
                    pGuard->message("本地保存位置已更新：" + oDirectory.m_strLabel);
                    bSelected = true;
                }
            }
            if (fnAfter)
            {
                fnAfter(bSelected);
            }
        });
    }

    void ClientWindow::downloadBookToLocal()
    {
        if (!m_bUnlocked || m_pSession->busy() || m_oBook.m_nId <= 0)
        {
            return;
        }
        const QString strDestination = m_oSettings.value("localDownloadDirectory").toString();
        if (strDestination.isEmpty() || !exportDirectoryAvailable(strDestination) ||
            insideCache(strDestination, m_strDirectory))
        {
            const int nBook = m_oBook.m_nId;
            chooseLocalDownloadDirectory([this, nBook](bool bSelected)
            {
                if (bSelected && m_bUnlocked && !m_pSession->busy() && m_oBook.m_nId == nBook)
                {
                    downloadBookToLocal();
                }
            });
            return;
        }
        const QString strId = m_pSession->downloads()->enqueue(m_oBook.m_nId,
            QString::fromUtf8(m_oBook.m_strTitle.c_str()), m_pIllustrations->isChecked(), strDestination);
        if (!strId.isEmpty())
        {
            m_pToast->showText("已加入后台下载，可在「下载缓存」查看进度", m_eTheme);
        }
    }

    void ClientWindow::showDownloadSettings()
    {
        auto* pPage = new QWidget;
        pPage->setObjectName("downloadSettingsPage");
        auto* pOuter = new QVBoxLayout(pPage);
        pOuter->setContentsMargins(0, 0, 0, 0);
        auto* pScroll = new QScrollArea;
        pScroll->setWidgetResizable(true);
        pScroll->setFrameShape(QFrame::NoFrame);
        pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* pBody = new QWidget;
        auto* pLayout = new QVBoxLayout(pBody);
        pLayout->setContentsMargins(16, 16, 16, 24);
        pLayout->setSpacing(10);
        auto fnParagraph = [pLayout](const QString& strText, const char* pName)
        {
            auto* pLabel = new QLabel(strText);
            pLabel->setObjectName(pName);
            pLabel->setTextFormat(Qt::PlainText);
            pLabel->setWordWrap(true);
            pLayout->addWidget(pLabel);
            return pLabel;
        };
        fnParagraph("两种保存方式，分别管理", "bookTitle");
        fnParagraph("下载缓存用于应用内离线阅读；下载到本地生成可自行使用的文件。", "muted");
        pLayout->addSpacing(12);
        fnParagraph("下载缓存", "sectionTitle");
        fnParagraph("保存在应用数据目录，仅供应用离线阅读，与用户选择的本地保存位置分开管理。"
                    "Android 使用应用私有存储；卸载应用会清除缓存。", "muted");
        pLayout->addSpacing(12);
        fnParagraph("下载到本地", "sectionTitle");
        fnParagraph("选择目录后，将小说保存为 UTF-8 文本和可离线打开的 HTML。"
                    "勾选包含插图时一并保存图片。每次新建独立书籍目录，已有文件不会被覆盖。"
                    "仅下载到本地不会加入下载缓存。", "muted");
        auto* pLocation = fnParagraph("", "localDownloadLocation");
        pLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const QPointer<QLabel> pLocationGuard(pLocation);
        auto fnUpdateLocation = [this, pLocationGuard]
        {
            if (!pLocationGuard)
            {
                return;
            }
            const QString strLocation = m_oSettings.value("localDownloadDirectory").toString();
            pLocationGuard->setText(strLocation.isEmpty()
                ? "尚未选择保存位置，首次下载到本地时会提示选择。"
                : "保存位置：" + exportDirectoryLabel(strLocation) +
                  (exportDirectoryAvailable(strLocation) ? "" : "\n目录不可用，请重新选择。"));
        };
        fnUpdateLocation();
        auto* pChoose = new QPushButton("选择保存目录");
        pChoose->setObjectName("chooseLocalDownloadDirectory");
        pChoose->setCursor(Qt::PointingHandCursor);
        pChoose->setMinimumHeight(48);
        setButtonIcon(pChoose, Icon::Folder);
        pLayout->addWidget(pChoose);
        connect(pChoose, &QPushButton::clicked, pPage, [this, fnUpdateLocation]
        {
            chooseLocalDownloadDirectory([fnUpdateLocation](bool)
            {
                fnUpdateLocation();
            });
        });
        const QString strLast = m_oSettings.value("lastLocalDownload").toString();
        if (!strLast.isEmpty())
        {
            fnParagraph("上次保存：" + strLast, "muted");
        }
        pLayout->addSpacing(12);
        fnParagraph("后台任务与断点恢复", "sectionTitle");
        fnParagraph("两种下载都在后台排队，不影响浏览和阅读。在「下载缓存」查看进度、暂停或继续。"
                    "已完成的章节与图片会实时保存；重新启动并验证原账号后，自动恢复未完成任务。"
                    "手动暂停的任务需点击继续。", "muted");
#ifdef Q_OS_ANDROID
        fnParagraph("系统休眠或结束应用时下载可能暂停，重新打开后继续。", "muted");
#endif
        pLayout->addStretch();
        pScroll->setWidget(pBody);
        pOuter->addWidget(pScroll);
        if (mobileMode())
        {
            (new TouchScroll(pScroll))->setEnabled(true);
            pushMobilePage(pPage, "下载与存储");
            return;
        }
        QDialog oDialog(this);
        oDialog.setWindowTitle("下载与存储");
        auto* pDialogLayout = new QVBoxLayout(&oDialog);
        pDialogLayout->addWidget(pPage);
        auto* pClose = new QPushButton("关闭");
        pClose->setMinimumHeight(40);
        pDialogLayout->addWidget(pClose, 0, Qt::AlignRight);
        connect(pClose, &QPushButton::clicked, &oDialog, &QDialog::accept);
        applyTheme(&oDialog, m_eTheme);
        oDialog.resize(qMin(600, width() - 40), qMin(560, height() - 40));
        oDialog.exec();
    }
}
