// SPDX-License-Identifier: GPL-2.0-only
#include "AppUpdatePanel.h"
#include "AppInfo.h"
#include "AppUpdateCheck.h"
#include "PageControls.h"
#include <QDesktopServices>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
namespace Wenku8::Reader
{
    AppUpdatePanel::AppUpdatePanel(QWidget* pParent) : QWidget(pParent)
    {
        setObjectName("appUpdatePanel");
        auto* pLayout = new QVBoxLayout(this);
        pLayout->setContentsMargins(0, 4, 0, 4);
        pLayout->setSpacing(8);
        auto* pVersion = PageControls::paragraph(
            QString("当前版本 %1").arg(applicationVersion()), "controlLabel");
        pVersion->setObjectName("updateCurrentVersion");
        pLayout->addWidget(pVersion);
        m_pStatus = PageControls::paragraph(QString());
        m_pStatus->setObjectName("updateCheckStatus");
        m_pStatus->setAccessibleName("更新检查状态");
        m_pStatus->hide();
        pLayout->addWidget(m_pStatus);
        m_pProgress = new QProgressBar;
        m_pProgress->setObjectName("updateCheckProgress");
        m_pProgress->setRange(0, 0);
        m_pProgress->setTextVisible(false);
        m_pProgress->setFixedHeight(4);
        m_pProgress->setAccessibleName("正在检查更新");
        m_pProgress->hide();
        pLayout->addWidget(m_pProgress);
        m_pCheckButton = PageControls::action("检查更新", "checkAppUpdates", Icon::Refresh);
        m_pCheckButton->setAccessibleName("检查轻小说文库更新");
        pLayout->addWidget(m_pCheckButton);
        m_pReleaseButton = PageControls::action(
            "前往发布页面", "openUpdateRelease", Icon::Discover);
        m_pReleaseButton->setAccessibleName("在浏览器中查看新版本发布页面");
        m_pReleaseButton->hide();
        pLayout->addWidget(m_pReleaseButton);
        m_pCheck = new AppUpdateCheck(this);
        m_pCheck->setObjectName("appUpdateCheck");
        connect(m_pCheckButton, &QPushButton::clicked, this, &AppUpdatePanel::startCheck);
        connect(m_pReleaseButton, &QPushButton::clicked, this, &AppUpdatePanel::openRelease);
        connect(m_pCheck, &AppUpdateCheck::finished, this,
                [this](const AppUpdateCheck::Result& oResult)
                {
                    m_bChecking = false;
                    m_pProgress->hide();
                    m_pCheckButton->setEnabled(true);
                    m_pCheckButton->setText("重新检查");
                    m_oReleaseUrl.clear();
                    m_pReleaseButton->hide();
                    using Status = AppUpdateCheck::Result::Status;
                    if (oResult.eStatus == Status::UpToDate)
                    {
                        m_pStatus->setText("没有可用的新版本。");
                    }
                    else if (oResult.eStatus == Status::UpdateAvailable)
                    {
                        m_pStatus->setText(QString("发现新版本 %1。").arg(oResult.strVersion));
                        m_oReleaseUrl = oResult.oReleaseUrl;
                        m_pReleaseButton->show();
                    }
                    else
                    {
                        m_pStatus->setText("暂时无法检查更新，请检查网络后重试。");
                    }
                });
    }

    void AppUpdatePanel::startCheck()
    {
        if (m_bChecking)
        {
            return;
        }
        m_bChecking = true;
        m_oReleaseUrl.clear();
        m_pReleaseButton->hide();
        m_pStatus->setText("正在检查更新…");
        m_pStatus->show();
        m_pCheckButton->setText("正在检查…");
        m_pCheckButton->setEnabled(false);
        m_pProgress->show();
        m_pCheck->check();
    }

    void AppUpdatePanel::openRelease()
    {
        if (!m_oReleaseUrl.isEmpty() && !QDesktopServices::openUrl(m_oReleaseUrl))
        {
            m_pStatus->setText("无法打开发布页面，请稍后重试。");
        }
    }
} // namespace Wenku8::Reader
