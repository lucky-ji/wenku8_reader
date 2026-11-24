// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "ReadAloudController.h"
#include "ReadAloudPanel.h"
#include "AnimatedStack.h"
#include "Icons.h"
#include "Toast.h"
#include "OnlineSession.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVariant>
#include <limits>

namespace Wenku8::Reader
{
    bool ClientWindow::speechPageActive() const
    {
        return m_pPages && (m_pPages->currentIndex() == 3 ||
                            (m_pPages->currentWidget() &&
                             m_pPages->currentWidget()->property("speechPage").toBool()));
    }

    void ClientWindow::initializeReadAloud()
    {
        // The controller owns the backend so its final stop precedes backend deletion.
        auto* pEngine = createSpeechEngine();
        m_pReadAloud = new ReadAloudController(pEngine, this);
        m_pReadAloud->setRate(m_oSettings.value("speech/rate", 1.0).toDouble());
        m_pReadAloud->setPitch(m_oSettings.value("speech/pitch", 1.0).toDouble());
        m_pReadAloud->setVolume(m_oSettings.value("speech/volume", 1.0).toDouble());
        m_pReadAloud->setFollow(m_oSettings.value("speech/follow", true).toBool());
        m_pReadAloud->setAutoNext(m_oSettings.value("speech/autoNext", true).toBool());
        // Network voice consent is deliberately renewed on each application launch.
        auto fnRestoreVoice = [this, pEngine]
        {
            if (m_bSpeechPreferencesReady || pEngine->voiceNames().isEmpty())
            {
                return;
            }
            m_bSpeechPreferencesReady = true;
            const QString strSaved = m_oSettings.value("speech/voice").toString();
            if (!strSaved.isEmpty() && pEngine->voiceNames().contains(strSaved))
            {
                m_pReadAloud->setVoice(strSaved);
            }
        };
        connect(pEngine, &SpeechEngine::readyChanged, this, fnRestoreVoice);
        fnRestoreVoice();
        connect(m_pReadAloud, &ReadAloudController::changed, this,
                [this]
                {
                    m_oSettings.setValue("speech/rate", m_pReadAloud->rate());
                    m_oSettings.setValue("speech/pitch", m_pReadAloud->pitch());
                    m_oSettings.setValue("speech/volume", m_pReadAloud->volume());
                    m_oSettings.setValue("speech/follow", m_pReadAloud->follow());
                    m_oSettings.setValue("speech/autoNext", m_pReadAloud->autoNext());
                    if (m_bSpeechPreferencesReady && !m_pReadAloud->engine()->voice().isEmpty())
                    {
                        m_oSettings.setValue("speech/voice", m_pReadAloud->engine()->voice());
                    }
                    updateSpeechMini();
                    updateVolumePaging();
                });
        connect(m_pReadAloud, &ReadAloudController::rangeChanged, this,
                [this](int nStart, int nEnd, bool bFollow)
                {
                    if (nStart < 0)
                    {
                        m_pReader->clearSpeechRange();
                        return;
                    }
                    if (!canReadBook() || m_pReadAloud->bookId() != m_oBook.m_nId || m_nChapter < 0 ||
                        m_nChapter >= m_vFlat.size() ||
                        m_pReadAloud->chapterId() != m_vFlat[m_nChapter].m_nId)
                    {
                        return;
                    }
                    m_pReader->setSpeechRange(nStart, nEnd, bFollow);
                    if (bFollow)
                    {
                        m_nMobileReaderOffset = nStart;
                        saveSpeechPosition();
                        updateReaderFooter();
                    }
                });
        connect(m_pReadAloud, &ReadAloudController::chapterRequested, this,
                &ClientWindow::requestSpeechChapter);
        connect(m_pReadAloud, &ReadAloudController::notice, this,
                [this](const QString& strNotice)
                {
                    if (m_pToast)
                    {
                        m_pToast->showText(strNotice, m_eTheme);
                    }
                    message(strNotice);
                });
        connect(m_pReader, &ReadingView::userNavigation, this,
                [this]
                {
                    m_pReadAloud->setFollow(false);
                });
        connect(m_pReader->verticalScrollBar(), &QScrollBar::sliderPressed, this,
                [this]
                {
                    m_pReadAloud->setFollow(false);
                });
        connect(m_pPages, &QStackedWidget::currentChanged, this,
                [this]
                {
                    if (!speechPageActive() &&
                        (m_pReadAloud->state() == ReadAloudController::State::Playing ||
                         m_pReadAloud->state() == ReadAloudController::State::WaitingChapter))
                    {
                        cancelSpeechChapter();
                        m_pReadAloud->pause("已离开阅读页面，听书已暂停");
                    }
                });
        connect(pEngine, &SpeechEngine::playbackAction, this,
                [this](const QString& strAction)
                {
                    if (!speechPageActive() || !canReadBook() || m_bLoadingBusy ||
                        QGuiApplication::applicationState() != Qt::ApplicationActive)
                    {
                        return;
                    }
                    if (strAction == "play")
                    {
                        m_pReadAloud->play();
                    }
                    else if (strAction == "pause")
                    {
                        m_pReadAloud->pause();
                    }
                    else if (strAction == "next" || strAction == "previous")
                    {
                        m_pReadAloud->step(strAction == "next" ? 1 : -1);
                    }
                });
    }

    QWidget* ClientWindow::createSpeechMini()
    {
        m_pSpeechMini = new QWidget;
        m_pSpeechMini->setObjectName("speechMini");
        m_pSpeechMini->setProperty("controlGroup", true);
        m_pSpeechMini->setAttribute(Qt::WA_StyledBackground);
        auto* pLayout = new QHBoxLayout(m_pSpeechMini);
        pLayout->setContentsMargins(6, 4, 6, 4);
        pLayout->setSpacing(4);
        auto* pOpen = new QPushButton;
        pOpen->setObjectName("speechMiniOpen");
        pOpen->setAccessibleName("打开听书设置");
        pOpen->setToolTip("听书设置");
        setButtonIcon(pOpen, Icon::Headphones);
        m_pSpeechMiniText = new QLabel;
        m_pSpeechMiniText->setTextFormat(Qt::PlainText);
        m_pSpeechMiniText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pSpeechMiniText->setObjectName("speechMiniStatus");
        m_pSpeechMiniPlay = new QPushButton;
        m_pSpeechMiniPlay->setObjectName("speechMiniPlay");
        setButtonIcon(m_pSpeechMiniPlay, Icon::Play);
        auto* pStop = new QPushButton;
        pStop->setObjectName("speechMiniStop");
        pStop->setAccessibleName("停止听书");
        pStop->setToolTip("停止听书");
        setButtonIcon(pStop, Icon::Stop);
        for (auto* pAction : {pOpen, m_pSpeechMiniPlay, pStop})
        {
            pAction->setFixedWidth(48);
            pAction->setMinimumHeight(48);
            pAction->setProperty("textAction", true);
            pAction->setProperty("keepIcon", true);
            pAction->setProperty("toolbarAction", true);
        }
        pLayout->addWidget(pOpen);
        pLayout->addWidget(m_pSpeechMiniText, 1);
        pLayout->addWidget(m_pSpeechMiniPlay);
        pLayout->addWidget(pStop);
        connect(pOpen, &QPushButton::clicked, this, &ClientWindow::showReadAloud);
        connect(m_pSpeechMiniPlay, &QPushButton::clicked, this,
                [this]
                {
                    if (!m_pReadAloud || m_bLoadingBusy)
                    {
                        return;
                    }
                    if (m_pReadAloud->state() == ReadAloudController::State::Playing ||
                        m_pReadAloud->state() == ReadAloudController::State::WaitingChapter)
                    {
                        cancelSpeechChapter();
                        m_pReadAloud->pause();
                    }
                    else
                    {
                        m_pReadAloud->play();
                    }
                });
        connect(pStop, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pReadAloud)
                    {
                        cancelSpeechChapter();
                        m_pReadAloud->stop();
                    }
                });
        m_pSpeechMini->hide();
        return m_pSpeechMini;
    }

    void ClientWindow::updateSpeechMini()
    {
        if (!m_pReadAloud || !m_pSpeechMini)
        {
            return;
        }
        const auto eState = m_pReadAloud->state();
        const bool bActive = eState == ReadAloudController::State::Playing ||
                             eState == ReadAloudController::State::WaitingChapter;
        m_pSpeechMini->setVisible(eState != ReadAloudController::State::Stopped &&
                                  m_pReadAloud->count() > 0);
        m_pSpeechMiniText->setText(QString("%1 · %2/%3")
                                       .arg(bActive ? "听书中" : "听书已暂停")
                                       .arg(m_pReadAloud->index() + 1)
                                       .arg(m_pReadAloud->count()));
        m_pSpeechMiniText->setToolTip(m_pReadAloud->status());
        m_pSpeechMiniText->setAccessibleName(m_pReadAloud->status());
        const QString strAction = bActive ? "暂停听书" : "继续听书";
        m_pSpeechMiniPlay->setAccessibleName(strAction);
        m_pSpeechMiniPlay->setToolTip(strAction);
        setButtonIcon(m_pSpeechMiniPlay, bActive ? Icon::Pause : Icon::Play);
    }

    void ClientWindow::showReadAloud()
    {
        if (!canReadBook() || m_bLoadingBusy || m_bRestoring || !m_pReadAloud || m_nChapter < 0 ||
            m_nChapter >= m_vFlat.size())
        {
            return;
        }
        if (m_pPages->currentWidget()->property("speechPage").toBool())
        {
            return;
        }
        if (m_pPages->currentIndex() != 3)
        {
            return;
        }
        if (m_pReadAloud->bookId() != m_oBook.m_nId ||
            m_pReadAloud->chapterId() != m_vFlat[m_nChapter].m_nId)
        {
            m_pReadAloud->loadChapter(m_oBook.m_nId, m_vFlat[m_nChapter].m_nId,
                                      m_pReader->document()->toPlainText(),
                                      m_pReader->readingPosition());
        }
        auto* pPanel = new ReadAloudPanel(m_pReadAloud, mobileMode());
        pPanel->setProperty("speechPage", true);
        pPanel->setChapterTitle(QString::fromStdString(m_vFlat[m_nChapter].m_strTitle));
        connect(pPanel, &ReadAloudPanel::seekReadingPosition, this,
                [this, pPanel]
                {
                    if (!canReadBook() || m_bLoadingBusy || !pPanel->isVisible() ||
                        !speechPageActive())
                    {
                        return;
                    }
                    const int nOffset = m_pPages->currentIndex() == 3 ? m_pReader->readingPosition()
                                                                      : m_nMobileReaderOffset;
                    m_pReadAloud->seekOffset(nOffset);
                    m_pReadAloud->play();
                });
        if (mobileMode())
        {
            pushMobilePage(pPanel, "听书");
        }
        else
        {
            showReaderPanel(pPanel, "听书");
        }
    }

    void ClientWindow::cancelSpeechChapter()
    {
        m_nSpeechPendingBook = 0;
        m_nSpeechPendingChapter = -1;
        m_bReopenReadAloud = false;
    }

    void ClientWindow::requestSpeechChapter(int nDirection)
    {
        if (!canReadBook() || m_bLoadingBusy || !speechPageActive() ||
            QGuiApplication::applicationState() != Qt::ApplicationActive ||
            m_pReadAloud->bookId() != m_oBook.m_nId)
        {
            m_pReadAloud->pause("章节暂时无法加载，请稍后继续听书");
            return;
        }
        const int nTarget = m_nChapter + (nDirection < 0 ? -1 : 1);
        if (nTarget < 0 || nTarget >= m_vFlat.size())
        {
            m_pReadAloud->stop();
            if (m_pToast)
            {
                m_pToast->showText(nTarget < 0 ? "已到本书开头" : "本书朗读完成", m_eTheme);
            }
            return;
        }
        m_bReopenReadAloud = m_pPages->currentWidget()->property("speechPage").toBool();
        // Unwind secondary mobile pages before replacing the underlying chapter.
        // Otherwise Back could remove the reader itself from the stacked widget.
        clearMobilePages();
        m_nSpeechPendingBook = m_oBook.m_nId;
        m_nSpeechPendingChapter = nTarget;
        m_nRestoreOffset = nDirection < 0 ? std::numeric_limits<int>::max() : 0;
        QScopedValueRollback<bool> oSpeechChange(m_bSpeechChangingChapter, true);
        openChapter(nTarget);
    }

    void ClientWindow::saveSpeechPosition()
    {
        if (!m_pReadAloud || !m_pReadAloud->follow() || !canReadBook() || m_bRestoring ||
            (m_pReadAloud->state() != ReadAloudController::State::Playing &&
             m_pReadAloud->state() != ReadAloudController::State::WaitingChapter) ||
            m_nChapter < 0 || m_nChapter >= m_vFlat.size() ||
            m_pReadAloud->bookId() != m_oBook.m_nId ||
            m_pReadAloud->chapterId() != m_vFlat[m_nChapter].m_nId)
        {
            return;
        }
        ReadingEntry oEntry;
        oEntry.m_nNovel = m_oBook.m_nId;
        oEntry.m_strTitle = QString::fromStdString(m_oBook.m_strTitle);
        oEntry.m_strAuthor = QString::fromStdString(m_oBook.m_strAuthor);
        oEntry.m_nChapter = m_vFlat[m_nChapter].m_nId;
        oEntry.m_strChapterTitle = QString::fromStdString(m_vFlat[m_nChapter].m_strTitle);
        oEntry.m_nOffset = m_pReadAloud->startOffset();
        oEntry.m_strSnippet = m_pReader->readingSnippet(oEntry.m_nOffset);
        oEntry.m_fProgress =
            (m_nChapter +
             double(oEntry.m_nOffset) / qMax(1, m_pReader->document()->characterCount() - 1)) /
            qMax(1, m_vFlat.size());
        m_pReadingState->save(oEntry, false);
    }
} // namespace Wenku8::Reader
