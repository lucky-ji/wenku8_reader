// SPDX-License-Identifier: GPL-2.0-only
#include "LoginHost.h"
#include "Wenku8WebView.h"
#include "Icons.h"
#include "LoadingPanel.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QEvent>
static void initDesktopLoginResources()
{
    Q_INIT_RESOURCE(mobile_browser);
}
namespace Wenku8::Reader
{
    class DesktopLoginHost final : public LoginHost
    {
        QDialog m_oLogin;
        QtWebView::BrowserSession m_oBrowser;
        QLabel* m_pHint = new QLabel;
        bool m_bRequirePersistence = false;
        QString m_strRememberScript;
        QWidget* m_pBrowserContainer = nullptr;
        LoadingPanel* m_pLoading = nullptr;
        bool m_bPageReady = false;
        quint64 m_nNavigation = 0;
        // QPointer stays non-null until QObject's base destructor; callbacks can
        // arrive earlier while browser_ tears down. Keep an independent lifetime
        // token so those callbacks never access partially destroyed members.
        std::shared_ptr<bool> m_pCallbacksAlive = std::make_shared<bool>(true);
        void invalidateCallbacks()
        {
            if (!*m_pCallbacksAlive)
                return;
            *m_pCallbacksAlive = false;
            ++m_nNavigation;
            disconnect(m_oBrowser.view(), nullptr, this, nullptr);
            if (m_pBrowserContainer)
                m_pBrowserContainer->removeEventFilter(this);
            if (m_pLoading)
                m_pLoading->m_fnOnCancel = {};
        }
        void setLoading(bool bBusy)
        {
            if (bBusy)
                m_bPageReady = false;
            m_oBrowser.view()->setEnabled(!bBusy);
            if (m_pLoading)
            {
                m_pLoading->setGeometry(m_pBrowserContainer->rect());
                m_pLoading->setVisible(bBusy);
                if (bBusy)
                    m_pLoading->raise();
            }
        }
        bool eventFilter(QObject* pTarget, QEvent* pEvent) override
        {
            if (pTarget == m_pBrowserContainer && pEvent->type() == QEvent::Resize && m_pLoading)
                m_pLoading->setGeometry(m_pBrowserContainer->rect());
            return LoginHost::eventFilter(pTarget, pEvent);
        }

      public:
        DesktopLoginHost(QWidget* pOwner, const QUrl& oOrigin, const QString& strStorage)
            : m_oLogin(pOwner), m_oBrowser(oOrigin, &m_oLogin, strStorage)
        {
            m_bRequirePersistence = !strStorage.isEmpty();
            initDesktopLoginResources();
            QFile oRemember(":/mobile-browser/remember_login.js");
            if (oRemember.open(QIODevice::ReadOnly))
                m_strRememberScript = QString::fromUtf8(oRemember.readAll());
            QFile oPresentation(":/mobile-browser/login_ui.js");
            if (oPresentation.open(QIODevice::ReadOnly))
                m_strRememberScript += "\n;\n" + QString::fromUtf8(oPresentation.readAll());
            auto baEncoded =
                QJsonDocument(QJsonArray{oOrigin.toString(QUrl::RemovePath | QUrl::RemoveQuery |
                                                          QUrl::RemoveFragment)})
                    .toJson(QJsonDocument::Compact);
            m_strRememberScript.replace("ORIGIN",
                                        QString::fromUtf8(baEncoded.mid(1, baEncoded.size() - 2)));
            m_oLogin.setObjectName("loginDialog");
            m_oLogin.setWindowTitle("登录 · 轻小说文库");
            m_oLogin.setWindowModality(Qt::WindowModal);
            m_oLogin.resize(860, 720);
            auto pLayout = new QVBoxLayout(&m_oLogin);
            pLayout->setContentsMargins(24, 20, 24, 20);
            pLayout->setSpacing(14);
            auto pTitle = new QLabel("登录你的文库账号");
            pTitle->setObjectName("bookTitle");
            pLayout->addWidget(pTitle);
            auto pIntro = new QLabel("请在网站页面中完成登录与验证。成功后此窗口会自动关闭。");
            pIntro->setObjectName("muted");
            pLayout->addWidget(pIntro);
            m_pBrowserContainer = new QWidget;
            auto pBrowserLayout = new QVBoxLayout(m_pBrowserContainer);
            pBrowserLayout->setContentsMargins(0, 0, 0, 0);
            pBrowserLayout->addWidget(m_oBrowser.view());
            pLayout->addWidget(m_pBrowserContainer, 1);
            m_pLoading = new LoadingPanel(m_pBrowserContainer);
            m_pLoading->setMessage("正在打开登录页面…");
            m_pLoading->setCancelable(true);
            m_pLoading->m_fnOnCancel = [this]
            {
                m_oLogin.reject();
            };
            m_pLoading->hide();
            m_pBrowserContainer->installEventFilter(this);
            m_pHint->setWordWrap(true);
            m_pHint->setObjectName("loginHint");
            m_pHint->hide();
            pLayout->addWidget(m_pHint);
            // Login is verified automatically. No manual check/debug buttons are needed.
            connect(m_oBrowser.view(), &QWebEngineView::loadStarted, this,
                    [this]
                    {
                        ++m_nNavigation;
                        m_pHint->hide();
                        setLoading(true);
                    });
            connect(m_oBrowser.view(), &QWebEngineView::loadFinished, this,
                    [this](bool bOk)
                    {
                        const auto nNavigation = m_nNavigation;
                        QPointer<DesktopLoginHost> pSelf(this);
                        auto pAlive = m_pCallbacksAlive;
                        if (!bOk)
                        {
                            setLoading(false);
                            m_pHint->setText("暂时无法打开登录页面，请检查网络后重新登录。");
                            m_pHint->show();
                            return;
                        }
                        m_oBrowser.view()->page()->runJavaScript(
                            m_strRememberScript, QWebEngineScript::ApplicationWorld,
                            [pSelf, nNavigation, pAlive](const QVariant&)
                            {
                                if (!*pAlive || !pSelf || nNavigation != pSelf->m_nNavigation)
                                    return;
                                pSelf->m_bPageReady = true;
                                pSelf->setLoading(false);
                                emit pSelf->pageLoaded();
                            });
                    });
            connect(&m_oLogin, &QDialog::rejected, this, &LoginHost::cancelled);
        }
        ~DesktopLoginHost() override
        {
            invalidateCallbacks();
        }
        bool visible() const override
        {
            return m_oLogin.isVisible();
        }
        bool hasPage() const override
        {
            return m_bPageReady && !m_oBrowser.view()->url().host().isEmpty();
        }
        bool persistent() const override
        {
            return m_oBrowser.persistent();
        }
        void show(bool bReset) override
        {
            if (m_bRequirePersistence && !persistent())
            {
                m_oLogin.show();
                m_pHint->setText("登录状态正被其他客户端窗口占用，请关闭旧窗口后重新启动。为避免丢"
                                 "失登录状态，此窗口不会使用临时会话登录。");
                m_oBrowser.view()->setEnabled(false);
                m_pHint->show();
                return;
            }
            if (bReset)
                m_oBrowser.reset();
            const bool bOpening = !visible();
            m_oLogin.show();
            m_oLogin.raise();
            m_oLogin.activateWindow();
            if (bOpening || bReset || !hasPage())
                openLogin();
        }
        void openLogin() override
        {
            m_oBrowser.openLogin();
        }
        void accept() override
        {
            m_oLogin.accept();
        }
        void close() override
        {
            invalidateCallbacks();
            m_oBrowser.close();
        }
        void setHint(const QString& strHint) override
        {
            if (!m_bRequirePersistence || persistent())
                m_pHint->setText(strHint);
        }
        void setTheme(Theme eTheme) override
        {
            applyTheme(&m_oLogin, eTheme);
            if (m_pLoading)
                m_pLoading->setTheme(eTheme);
            const auto oC = colors(eTheme);
            m_oBrowser.setPageStyle(QString(R"(
            ::-webkit-scrollbar { width:12px; height:12px; }
            ::-webkit-scrollbar-track { background:transparent; border-radius:8px; }
            ::-webkit-scrollbar-track:hover { background:%1; }
            ::-webkit-scrollbar-thumb { background:%2; border:3px solid transparent; background-clip:padding-box; border-radius:8px; min-height:40px; }
            ::-webkit-scrollbar-thumb:hover { background:%3; border:2px solid transparent; background-clip:padding-box; }
            ::-webkit-scrollbar-thumb:active { background:%4; }
            ::-webkit-scrollbar-button { display:none; width:0; height:0; }
            ::-webkit-scrollbar-corner { background:transparent; }
        )")
                                        .arg(oC.m_oSection.name(), oC.m_oDisabled.name(),
                                             oC.m_oMuted.name(), oC.m_oAccent.name()));
        }
        std::string userAgent() const override
        {
            return m_oBrowser.userAgent();
        }
        std::shared_ptr<HttpTransport> transport() const override
        {
            return m_oBrowser.transport();
        }
        std::shared_ptr<HttpTransport> downloadTransport() const override
        {
            return m_oBrowser.downloadTransport();
        }
    };
    std::unique_ptr<LoginHost> makeLoginHost(QWidget* pOwner, const QUrl& oOrigin,
                                             const QString& strStorage)
    {
        return std::make_unique<DesktopLoginHost>(pOwner, oOrigin, strStorage);
    }
} // namespace Wenku8::Reader
