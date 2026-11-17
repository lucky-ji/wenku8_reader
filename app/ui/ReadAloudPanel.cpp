// SPDX-License-Identifier: GPL-2.0-only
#include "ReadAloudPanel.h"
#include "ReadAloudController.h"
#include "PageControls.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QtMath>
#include <functional>
#include <algorithm>
namespace Wenku8::Reader
{
    using namespace PageControls;
    namespace
    {
        void setLabelText(QLabel* pLabel, const QString& strText)
        {
            if (pLabel->text() != strText)
            {
                pLabel->setText(strText);
            }
        }
        bool comboOpen(QComboBox* pCombo)
        {
            return pCombo->view()->isVisible();
        }
    } // namespace
    ReadAloudPanel::ReadAloudPanel(ReadAloudController* pController, bool bMobile, QWidget* pParent)
        : QWidget(pParent), m_pController(pController), m_bMobile(bMobile)
    {
        setObjectName("readAloudPanel");
        setProperty("touchUi", bMobile);
        auto* pOuter = new QVBoxLayout(this);
        pOuter->setContentsMargins(0, 0, 0, 0);
        QVBoxLayout* pLayout = nullptr;
        auto* pScrollPage = scrollPage(pLayout);
        pScrollPage->setProperty("touchUi", bMobile);
        pScrollPage->setObjectName("readAloudScrollPage");
        pLayout->setContentsMargins(bMobile ? 12 : 4, 12, bMobile ? 12 : 4, 20);
        pLayout->setSpacing(10);
        pOuter->addWidget(pScrollPage);
        if (!m_pController)
        {
            pLayout->addWidget(paragraph("听书暂时不可用，请返回阅读页后重试。"));
            setEnabled(false);
            return;
        }
        auto fnButton = [bMobile](const QString& strTitle, const char* pName, Icon eIcon)
        {
            auto* pButton = action(strTitle, pName, eIcon);
            pButton->setProperty("mobileRow", false);
            pButton->setProperty("controlAction", true);
            pButton->setMinimumHeight(bMobile ? 48 : 36);
            pButton->setMinimumWidth(0);
            pButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            return pButton;
        };
        m_pChapter = paragraph("本章听书", "readAloudChapter");
        pLayout->addWidget(m_pChapter);
        auto* pCurrent = controlGroup(pLayout);
        pCurrent->addWidget(paragraph("当前短段", "sectionTitle"));
        m_pPreview = paragraph("本章没有可朗读的正文。", "readAloudPreview");
        m_pPreview->setMinimumHeight(96);
        m_pPreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        pCurrent->addWidget(m_pPreview);
        m_pProgressText = paragraph("0 / 0 段", "readAloudProgressText");
        pCurrent->addWidget(m_pProgressText);
        m_pProgress = new QProgressBar;
        m_pProgress->setObjectName("readAloudProgress");
        m_pProgress->setTextVisible(false);
        m_pProgress->setFixedHeight(4);
        pCurrent->addWidget(m_pProgress);
        m_pStatus = paragraph("准备朗读", "readAloudStatus");
        m_pStatus->setAccessibleName("听书状态");
        pCurrent->addWidget(m_pStatus);
        auto* pPlayback = new QHBoxLayout;
        pPlayback->setSpacing(4);
        m_pPrevious = fnButton("上一段", "readAloudPrevious", Icon::Left);
        m_pToggle = fnButton("播放", "readAloudToggle", Icon::Play);
        m_pToggle->setProperty("primary", true);
        m_pNext = fnButton("下一段", "readAloudNext", Icon::Right);
        for (auto* pAction : {m_pPrevious, m_pToggle, m_pNext})
        {
            pAction->setProperty("toolbarAction", true);
        }
        pPlayback->addWidget(m_pPrevious, 1);
        pPlayback->addWidget(m_pToggle, 1);
        pPlayback->addWidget(m_pNext, 1);
        pCurrent->addLayout(pPlayback);
        m_pStop = fnButton("停止听书", "readAloudStop", Icon::Stop);
        pCurrent->addWidget(m_pStop);
        m_pFromReading = fnButton("从当前阅读位置开始", "readAloudFromReading", Icon::Headphones);
        pCurrent->addWidget(m_pFromReading);
        connect(m_pToggle, &QPushButton::clicked, this,
                [this]
                {
                    if (!m_pController)
                    {
                        return;
                    }
                    m_strLocalStatus.clear();
                    if (m_pController->state() == ReadAloudController::State::Playing ||
                        m_pController->state() == ReadAloudController::State::WaitingChapter)
                    {
                        m_pController->pause();
                    }
                    else
                    {
                        m_pController->play();
                    }
                });
        connect(m_pPrevious, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pController)
                    {
                        m_strLocalStatus.clear();
                        m_pController->step(-1);
                    }
                });
        connect(m_pNext, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pController)
                    {
                        m_strLocalStatus.clear();
                        m_pController->step(1);
                    }
                });
        connect(m_pStop, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pController)
                    {
                        m_strLocalStatus.clear();
                        m_pController->stop();
                    }
                });
        connect(m_pFromReading, &QPushButton::clicked, this,
                [this]
                {
                    m_strLocalStatus.clear();
                    emit seekReadingPosition();
                });
        pLayout->addWidget(paragraph("播放偏好", "sectionTitle"));
        auto* pPreferences = controlGroup(pLayout);
        m_pFollow = new QCheckBox("随朗读滚动正文");
        m_pFollow->setObjectName("readAloudFollow");
        m_pAutoNext = new QCheckBox("本章结束后继续下一章");
        m_pAutoNext->setObjectName("readAloudAutoNext");
        for (auto* pCheck : {m_pFollow, m_pAutoNext})
        {
            pCheck->setMinimumHeight(bMobile ? 48 : 36);
            pPreferences->addWidget(pCheck);
        }
        connect(m_pFollow, &QCheckBox::toggled, this,
                [this](bool bChecked)
                {
                    if (m_pController)
                    {
                        m_pController->setFollow(bChecked);
                    }
                });
        connect(m_pAutoNext, &QCheckBox::toggled, this,
                [this](bool bChecked)
                {
                    if (m_pController)
                    {
                        m_pController->setAutoNext(bChecked);
                    }
                });
        pPreferences->addWidget(paragraph("睡眠定时", "controlLabel"));
        m_pSleep = new QComboBox;
        m_pSleep->setObjectName("readAloudSleep");
        m_pSleep->setMinimumHeight(bMobile ? 48 : 36);
        m_pSleep->setPlaceholderText("定时进行中");
        m_pSleep->addItem("关闭定时", 0);
        m_pSleep->addItem("15 分钟后停止", 15);
        m_pSleep->addItem("30 分钟后停止", 30);
        m_pSleep->addItem("60 分钟后停止", 60);
        m_pSleep->addItem("本章结束后停止", -1);
        pPreferences->addWidget(m_pSleep);
        m_pSleepRemaining = paragraph("未设置定时", "readAloudSleepRemaining");
        pPreferences->addWidget(m_pSleepRemaining);
        connect(m_pSleep, QOverload<int>::of(&QComboBox::activated), this,
                [this](int nIndex)
                {
                    if (!m_pController || nIndex < 0)
                    {
                        return;
                    }
                    m_nSleepPreset = m_pSleep->itemData(nIndex).toInt();
                    if (m_nSleepPreset == -1)
                    {
                        m_pController->setStopAtChapter(true);
                    }
                    else
                    {
                        m_pController->setSleepMinutes(m_nSleepPreset);
                    }
                });
        pLayout->addWidget(paragraph("系统音色", "sectionTitle"));
        auto* pVoices = controlGroup(pLayout);
        m_pVoice = new QComboBox;
        m_pVoice->setObjectName("readAloudVoice");
        m_pVoice->setMinimumWidth(0);
        m_pVoice->setMinimumHeight(bMobile ? 48 : 36);
        m_pVoice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_pVoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_pVoice->setMinimumContentsLength(8);
        m_pVoice->setPlaceholderText("系统音色尚未就绪");
        pVoices->addWidget(m_pVoice);
        m_pNetworkConsent = new QCheckBox("允许联网系统音色");
        m_pNetworkConsent->setObjectName("readAloudNetworkConsent");
        m_pNetworkConsent->setMinimumHeight(bMobile ? 48 : 36);
        pVoices->addWidget(m_pNetworkConsent);
        m_pNetworkHint = paragraph("", "readAloudNetworkHint");
        pVoices->addWidget(m_pNetworkHint);
        m_pEngineSettings = fnButton("系统语音设置", "readAloudEngineSettings", Icon::Settings);
        pVoices->addWidget(m_pEngineSettings);
        connect(m_pVoice, QOverload<int>::of(&QComboBox::activated), this,
                [this](int nIndex)
                {
                    if (m_pController && nIndex >= 0)
                    {
                        m_strLocalStatus.clear();
                        m_pController->setVoice(m_pVoice->itemText(nIndex));
                        updateState();
                    }
                });
        connect(m_pNetworkConsent, &QCheckBox::toggled, this,
                [this](bool bChecked)
                {
                    if (m_pController)
                    {
                        m_pController->setAllowNetworkVoice(bChecked);
                    }
                });
        connect(m_pEngineSettings, &QPushButton::clicked, this,
                [this]
                {
                    if (m_pController && m_pController->engine() &&
                        !m_pController->engine()->openSettings())
                    {
                        m_strLocalStatus = "无法打开系统语音设置，请在设备设置中选择语音服务。";
                        updateState();
                    }
                });
        pLayout->addWidget(paragraph("声音调节", "sectionTitle"));
        auto* pSound = controlGroup(pLayout);
        auto fnSlider = [this, pSound, bMobile](const QString& strTitle, const char* pName,
                                                int nMinimum, int nMaximum, QLabel*& pValue,
                                                const std::function<void(int)>& fnApply)
        {
            auto* pHeading = new QHBoxLayout;
            pHeading->addWidget(paragraph(strTitle, "controlLabel"), 1);
            pValue = paragraph("");
            pValue->setObjectName(QString::fromUtf8(pName) + "Value");
            pHeading->addWidget(pValue);
            pSound->addLayout(pHeading);
            auto* pSlider = new QSlider(Qt::Horizontal);
            pSlider->setObjectName(pName);
            pSlider->setProperty("speechControlSlider", true);
            pSlider->setRange(nMinimum, nMaximum);
            pSlider->setSingleStep(5);
            pSlider->setPageStep(10);
            pSlider->setMinimumHeight(bMobile ? 48 : 36);
            pSlider->setLayoutDirection(Qt::LeftToRight);
            pSlider->setInvertedAppearance(false);
            pSlider->setAccessibleName(strTitle);
            pSound->addWidget(pSlider);
            connect(pSlider, &QSlider::valueChanged, this,
                    [pSlider, pValue, fnApply, nMaximum](int nValue)
                    {
                        setLabelText(pValue, nMaximum == 100
                                                 ? QString::number(nValue) + "%"
                                                 : QString::number(nValue / 100.0, 'f', 2) + "×");
                        if (!pSlider->isSliderDown())
                        {
                            fnApply(nValue);
                        }
                    });
            connect(pSlider, &QSlider::sliderReleased, this,
                    [pSlider, fnApply]
                    {
                        fnApply(pSlider->value());
                    });
            return pSlider;
        };
        m_pRate = fnSlider("语速", "readAloudRate", 50, 200, m_pRateValue,
                           [this](int nValue)
                           {
                               if (m_pController)
                               {
                                   m_pController->setRate(nValue / 100.0);
                               }
                           });
        m_pPitch = fnSlider("音调", "readAloudPitch", 50, 200, m_pPitchValue,
                            [this](int nValue)
                            {
                                if (m_pController)
                                {
                                    m_pController->setPitch(nValue / 100.0);
                                }
                            });
        m_pVolume = fnSlider("朗读音量", "readAloudVolume", 0, 100, m_pVolumeValue,
                             [this](int nValue)
                             {
                                 if (m_pController)
                                 {
                                     m_pController->setVolume(nValue / 100.0);
                                 }
                             });
        pLayout->addWidget(
            paragraph("仅前台听书，切到后台会暂停。暂停后继续会从当前短段开头重读。"));
        pLayout->addStretch();
        connect(m_pController, &ReadAloudController::changed, this, &ReadAloudPanel::updateState);
        connect(m_pController, &QObject::destroyed, this,
                [this]
                {
                    setEnabled(false);
                });
        if (auto* pEngine = m_pController->engine())
        {
            connect(pEngine, &SpeechEngine::readyChanged, this,
                    [this]
                    {
                        refreshVoices();
                        updateState();
                    });
        }
        refreshVoices();
        updateState();
    }
    void ReadAloudPanel::setChapterTitle(const QString& strTitle)
    {
        if (m_pChapter)
        {
            setLabelText(m_pChapter, strTitle.isEmpty() ? QStringLiteral("本章听书") : strTitle);
        }
    }
    void ReadAloudPanel::refreshVoices()
    {
        if (!m_pController || !m_pVoice)
        {
            return;
        }
        if (comboOpen(m_pVoice))
        {
            m_bVoiceRefreshPending = true;
            QTimer::singleShot(200, this,
                               [this]
                               {
                                   if (m_bVoiceRefreshPending)
                                   {
                                       refreshVoices();
                                   }
                               });
            return;
        }
        m_bVoiceRefreshPending = false;
        auto* pEngine = m_pController->engine();
        const QStringList vNames = pEngine ? pEngine->voiceNames() : QStringList();
        QStringList vCurrent;
        for (int nIndex = 0; nIndex < m_pVoice->count(); ++nIndex)
        {
            vCurrent.append(m_pVoice->itemText(nIndex));
        }
        const QSignalBlocker oBlocker(m_pVoice);
        if (vCurrent.size() != vNames.size() ||
            !std::equal(vCurrent.cbegin(), vCurrent.cend(), vNames.cbegin()))
        {
            m_pVoice->clear();
            m_pVoice->addItems(vNames);
        }
        m_pVoice->setCurrentIndex(pEngine ? m_pVoice->findText(pEngine->voice()) : -1);
        m_pVoice->setEnabled(!vNames.isEmpty());
        m_pVoice->setToolTip(pEngine ? pEngine->voice() : QString());
    }
    void ReadAloudPanel::updateState()
    {
        if (!m_pController)
        {
            return;
        }
        using State = ReadAloudController::State;
        const auto eState = m_pController->state();
        const bool bPlaying = eState == State::Playing || eState == State::WaitingChapter;
        const bool bWaiting = eState == State::WaitingChapter;
        const int nCount = m_pController->count();
        const int nIndex = nCount > 0 ? qBound(0, m_pController->index(), nCount - 1) : 0;
        auto* pEngine = m_pController->engine();
        const bool bNeedsNetwork = pEngine && pEngine->requiresNetwork();
        const bool bCanPlay = nCount > 0 && pEngine && pEngine->available() &&
                              (!bNeedsNetwork || m_pController->allowNetworkVoice());
        setLabelText(m_pPreview, nCount > 0 ? m_pController->currentText()
                                            : QStringLiteral("本章没有可朗读的正文。"));
        setLabelText(m_pProgressText, nCount > 0
                                          ? QString("第 %1 / %2 段").arg(nIndex + 1).arg(nCount)
                                          : QStringLiteral("0 / 0 段"));
        m_pProgress->setRange(0, qMax(1, nCount));
        m_pProgress->setValue(nCount > 0 ? nIndex + 1 : 0);
        QString strStatus = m_pController->status();
        if (!m_strLocalStatus.isEmpty())
        {
            strStatus = m_strLocalStatus;
        }
        else if (!bPlaying && pEngine && !pEngine->available())
        {
            strStatus = pEngine->errorString().isEmpty() ? QStringLiteral("系统语音尚未就绪。")
                                                         : pEngine->errorString();
        }
        else if (!bPlaying && bNeedsNetwork && !m_pController->allowNetworkVoice())
        {
            strStatus = "所选音色需要联网，请先允许联网合成或选择离线音色。";
        }
        else if (!bPlaying && pEngine && !pEngine->errorString().isEmpty())
        {
            strStatus = pEngine->errorString();
        }
        setLabelText(m_pStatus,
                     strStatus.isEmpty() ? QStringLiteral("点击播放开始听书。") : strStatus);
        const QString strToggle = bPlaying                  ? QStringLiteral("暂停")
                                  : eState == State::Paused ? QStringLiteral("继续")
                                  : eState == State::Error  ? QStringLiteral("重试")
                                                            : QStringLiteral("播放");
        if (m_pToggle->text() != strToggle)
        {
            m_pToggle->setText(strToggle);
        }
        if (m_bPauseButton != bPlaying)
        {
            m_bPauseButton = bPlaying;
            setButtonIcon(m_pToggle, bPlaying ? Icon::Pause : Icon::Play);
        }
        m_pToggle->setEnabled(bPlaying || bCanPlay);
        m_pPrevious->setEnabled(nCount > 0 && !bWaiting &&
                                (nIndex > 0 || m_pController->autoNext()));
        m_pNext->setEnabled(nCount > 0 && !bWaiting &&
                            (nIndex + 1 < nCount || m_pController->autoNext()));
        m_pStop->setEnabled(eState != State::Stopped ||
                            m_pController->remainingSleepSeconds() > 0 ||
                            m_pController->stopAtChapter());
        m_pFromReading->setEnabled(bCanPlay && !bWaiting);
        for (auto oCheck : {qMakePair(m_pFollow, m_pController->follow()),
                            qMakePair(m_pAutoNext, m_pController->autoNext()),
                            qMakePair(m_pNetworkConsent, m_pController->allowNetworkVoice())})
        {
            const QSignalBlocker oBlocker(oCheck.first);
            oCheck.first->setChecked(oCheck.second);
        }
        setLabelText(
            m_pNetworkHint,
            bNeedsNetwork
                ? QStringLiteral(
                      "当前音色标为[需联网]。允许后，待朗读文字会交给所选系统语音服务处理。")
                : QStringLiteral("使用系统语音服务。是否联网取决于系统引擎和音色；标有[需联网]"
                                 "的音色须先允许。"));
        m_pEngineSettings->setVisible(pEngine && pEngine->canOpenSettings());
        const int nRemaining = m_pController->remainingSleepSeconds();
        if (m_pController->stopAtChapter())
        {
            m_nSleepPreset = -1;
            setLabelText(m_pSleepRemaining, "本章结束后停止");
        }
        else if (nRemaining > 0)
        {
            if (m_nSleepPreset == -1)
            {
                m_nSleepPreset = 0;
            }
            setLabelText(m_pSleepRemaining, QString("剩余 %1:%2")
                                                .arg(nRemaining / 60, 2, 10, QChar('0'))
                                                .arg(nRemaining % 60, 2, 10, QChar('0')));
        }
        else
        {
            m_nSleepPreset = 0;
            setLabelText(m_pSleepRemaining, "未设置定时");
        }
        if (!comboOpen(m_pSleep))
        {
            const QSignalBlocker oBlocker(m_pSleep);
            m_pSleep->setCurrentIndex(
                nRemaining > 0 && m_nSleepPreset == 0 ? -1 : m_pSleep->findData(m_nSleepPreset));
        }
        if (!comboOpen(m_pVoice) && pEngine)
        {
            const QSignalBlocker oBlocker(m_pVoice);
            m_pVoice->setCurrentIndex(m_pVoice->findText(pEngine->voice()));
            m_pVoice->setToolTip(pEngine->voice());
        }
        auto fnUpdateSlider = [](QSlider* pSlider, QLabel* pValue, int nValue, bool bPercent)
        {
            if (!pSlider->isSliderDown())
            {
                const QSignalBlocker oBlocker(pSlider);
                pSlider->setValue(nValue);
                setLabelText(pValue, bPercent ? QString::number(nValue) + "%"
                                              : QString::number(nValue / 100.0, 'f', 2) + "×");
            }
        };
        fnUpdateSlider(m_pRate, m_pRateValue, qRound(m_pController->rate() * 100), false);
        fnUpdateSlider(m_pPitch, m_pPitchValue, qRound(m_pController->pitch() * 100), false);
        fnUpdateSlider(m_pVolume, m_pVolumeValue, qRound(m_pController->volume() * 100), true);
    }
} // namespace Wenku8::Reader
