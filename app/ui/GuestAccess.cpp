// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#include "ReadAloudController.h"
#ifdef WENKU8_CLIENT_ONLINE
#include "OnlineSession.h"
#endif
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <utility>

namespace Wenku8::Reader
{
    void ClientWindow::start(int nNovel)
    {
        if (nNovel > 0)
        {
            m_nStartupNovel = nNovel;
            try
            {
                m_pStore->novel(nNovel);
                navigate(2);
                openBook(nNovel);
            }
            catch (const std::exception&)
            {
                // Uncached startup novels can open after successful login.
            }
        }
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession && m_pSession->persistent())
        {
            QTimer::singleShot(0, m_pSession, &OnlineSession::restoreSession);
        }
#endif
    }

    bool ClientWindow::canReadBook() const
    {
        return m_oBook.m_nId > 0 && (m_bLocalBook || m_bAuthenticated);
    }

    void ClientWindow::setLibraryHeading()
    {
        static const QStringList vTitles{"我的书架", "最近更新", "下载缓存", "阅读历史"};
        static const QStringList vSubtitles{"查看收藏的小说。", "按文库更新顺序浏览小说。",
                                            "打开已下载的小说，支持离线阅读。",
                                            "继续上次的阅读或查看读过的小说。"};
        m_pTitle->setText(vTitles[m_nSection]);
        m_pSubtitle->setText(vSubtitles[m_nSection]);
        m_pGuestTitle->setText(m_nSection == 1 ? "登录后查看最近更新" : "登录后查看书架");
    }

    void ClientWindow::enterLibrarySection()
    {
        const bool bNeedsLogin = m_nSection < 2 && !m_bAuthenticated;
        m_pPages->setCurrentIndex(bNeedsLogin ? 0 : 1);
        if (!bNeedsLogin)
        {
            refresh();
        }
        adaptLayout();
    }

    void ClientWindow::updateAccountControls()
    {
        QString strName = "当前构建未启用网页登录";
        bool bCanSignIn = false;
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession)
        {
            bCanSignIn = true;
            if (m_bAuthenticated)
            {
                strName = m_pSession->username();
            }
            else
            {
                strName = m_pSession->restoringSession() ? "正在确认已保存的登录状态…" : "尚未登录";
            }
        }
#endif
        for (auto* pProfile : findChildren<QLabel*>("mobileProfileName"))
        {
            pProfile->setText(strName);
        }
        for (auto* pLogin : findChildren<QPushButton*>("mobileSignIn"))
        {
            pLogin->setText(m_bAuthenticated ? "切换账号" : "登录文库");
            pLogin->setEnabled(bCanSignIn && !m_bLoadingBusy);
        }
    }

    void ClientWindow::signInFromAccount()
    {
#ifdef WENKU8_CLIENT_ONLINE
        if (m_pSession && !m_bLoadingBusy)
        {
            m_bReturnAfterLogin =
                m_nSection < 2 && m_vMobileHistory.size() == 1 && m_vMobileHistory.first() <= 1;
            m_pSession->signIn(m_bAuthenticated);
        }
#endif
    }

    void ClientWindow::authenticate(bool bValue)
    {
        if (m_bAuthenticated == bValue)
        {
            return;
        }
        const int nPage = m_pPages->currentIndex();
        const bool bOnlineBook = !m_bLocalBook && m_oBook.m_nId > 0 &&
                                 (nPage == 2 || nPage == 3 || m_vMobileHistory.contains(2) ||
                                  m_vMobileHistory.contains(3));
        if (!bValue && bOnlineBook)
        {
            saveProgress();
            saveSpeechPosition();
            cancelSpeechChapter();
            m_pReadAloud->stop();
        }
        m_bAuthenticated = bValue;
        updateAccountControls();
        if (bValue)
        {
            updateBookActions();
            m_pCovers->retryFailures();
            QTimer::singleShot(0, this,
                               [this]
                               {
                                   const bool bReturn = std::exchange(m_bReturnAfterLogin, false);
                                   if (!m_bAuthenticated)
                                   {
                                       return;
                                   }
                                   if (bReturn && m_pPages->currentWidget()->objectName() ==
                                                      "mobileAccountPage")
                                   {
                                       popMobilePage();
                                   }
                                   else if (m_pPages->currentIndex() == 0)
                                   {
                                       enterLibrarySection();
                                   }
                                   else
                                   {
                                       adaptLayout();
                                   }
                               });
            return;
        }

        ++m_nBusinessContext;
        m_vShelfIds.clear();
        resetDiscoveryFeed();
        updateBookActions();
        const bool bHadSearch = !m_strActiveSearch.isEmpty();
        if (m_nSection < 2 || bHadSearch)
        {
            m_oLibraryReturn = {};
            m_strActiveSearch.clear();
            m_pSearch->clear();
            m_pCards->clear();
        }
        if (m_nSection < 2 && !m_vMobileHistory.isEmpty() && m_vMobileHistory.first() == 1)
        {
            m_vMobileHistory.first() = 0;
        }
        if (bOnlineBook)
        {
            clearMobilePages();
            leaveReaderFullscreen();
            ++m_nRestoreGeneration;
            m_bRestoring = false;
            m_vFlat.clear();
            m_oBook = {};
            m_nChapter = -1;
            m_nPendingChapter = -1;
            m_pReader->showChapter({}, {}, {});
            m_pToc->clear();
        }
        if (nPage <= 1 || bOnlineBook || bHadSearch)
        {
            setLibraryHeading();
        }
        if (bHadSearch && m_nSection >= 2)
        {
            if (m_nSection == 2)
            {
                downloadedBooks();
            }
            else
            {
                historyBooks();
            }
        }
        if (nPage <= 1 || bOnlineBook)
        {
            enterLibrarySection();
        }
        else
        {
            adaptLayout();
        }
    }
} // namespace Wenku8::Reader
