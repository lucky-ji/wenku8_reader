// SPDX-License-Identifier: GPL-2.0-only
#include "SleepTimerControl.h"
#include "ReadAloudController.h"
#include "PageControls.h"
#include <QGridLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QLocale>
#include <QTimer>
#include <QGuiApplication>
#include <QInputMethod>
namespace Wenku8::Reader
{
    using namespace PageControls;
    SleepTimerControl::SleepTimerControl(ReadAloudController* pController, bool bMobile,
                                         QWidget* pParent)
        : QWidget(pParent), m_pController(pController)
    {
        setObjectName("sleepTimerControl");
        setProperty("controlGroup", true);
        setProperty("settingsSection", true);
        setAttribute(Qt::WA_StyledBackground);
        auto* pLayout = new QVBoxLayout(this);
        pLayout->setContentsMargins(14, 14, 14, 14);
        pLayout->setSpacing(10);
        pLayout->addWidget(paragraph("定时关闭", "settingsSectionTitle"));
        pLayout->addWidget(paragraph("到时停止朗读，保留当前阅读位置。", "settingsHint"));
        m_pRemaining = paragraph("未设置定时", "readAloudSleepRemaining");
        m_pRemaining->setAccessibleName("听书剩余时间");
        m_pRemaining->setProperty("settingsStatus", true);
        pLayout->addWidget(m_pRemaining);
        auto* pChoices = new QGridLayout;
        pChoices->setSpacing(6);
        const QVector<QPair<QString, int>> vModes{{"15 分钟", 15}, {"30 分钟", 30},
                                                  {"60 分钟", 60}, {"本章结束", -1},
                                                  {"自定义", -2},  {"关闭定时", 0}};
        for (int nIndex = 0; nIndex < vModes.size(); ++nIndex)
        {
            const auto& oMode = vModes[nIndex];
            auto* pChoice = new QPushButton(oMode.first);
            pChoice->setObjectName("readAloudSleepChoice" + QString::number(oMode.second));
            pChoice->setAccessibleName(oMode.first);
            pChoice->setProperty("timerMinutes", oMode.second);
            pChoice->setProperty("settingsChoice", true);
            pChoice->setMinimumHeight(bMobile ? 48 : 40);
            pChoice->setMinimumWidth(0);
            pChoice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            pChoice->setCursor(Qt::PointingHandCursor);
            pChoice->setCheckable(true);
            pChoices->addWidget(pChoice, nIndex / 3, nIndex % 3);
            m_vChoices.append(pChoice);
            connect(pChoice, &QPushButton::clicked, this,
                    [this, nMode = oMode.second]
                    {
                        chooseMode(nMode);
                    });
        }
        for (int nColumn = 0; nColumn < 3; ++nColumn)
        {
            pChoices->setColumnStretch(nColumn, 1);
        }
        pLayout->addLayout(pChoices);
        m_pCustom = new QWidget;
        m_pCustom->setObjectName("readAloudCustomTimer");
        auto* pCustomLayout = new QVBoxLayout(m_pCustom);
        pCustomLayout->setContentsMargins(0, 4, 0, 0);
        pCustomLayout->setSpacing(8);
        auto* pDuration = new QWidget;
        pDuration->setProperty("settingsStepper", true);
        pDuration->setAttribute(Qt::WA_StyledBackground);
        auto* pDurationLayout = new QHBoxLayout(pDuration);
        pDurationLayout->setContentsMargins(2, 2, 2, 2);
        pDurationLayout->setSpacing(4);
        m_nCustomMinutes = qBound(1, m_oSettings.value("speech/customSleepMinutes", 45).toInt(),
                                  ReadAloudController::maximumSleepMinutes());
        m_pMinutes = new QLineEdit(QString::number(m_nCustomMinutes));
        m_pMinutes->setObjectName("readAloudCustomMinutes");
        m_pMinutes->setAccessibleName("自定义定时分钟数");
        auto* pValidator =
            new QIntValidator(1, ReadAloudController::maximumSleepMinutes(), m_pMinutes);
        QLocale oDigits(QLocale::C);
        oDigits.setNumberOptions(QLocale::RejectGroupSeparator);
        pValidator->setLocale(oDigits);
        m_pMinutes->setValidator(pValidator);
        m_pMinutes->setMaxLength(4);
        m_pMinutes->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText);
        m_pMinutes->setAlignment(Qt::AlignCenter);
        m_pMinutes->setMinimumWidth(0);
        m_pMinutes->setMinimumHeight(bMobile ? 48 : 40);
        m_pMinutes->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_pMinutes->setProperty("settingsNumberInput", true);
        auto* pDecrease = new QPushButton("−");
        auto* pIncrease = new QPushButton("＋");
        pDecrease->setObjectName("readAloudCustomDecrease");
        pIncrease->setObjectName("readAloudCustomIncrease");
        for (auto* pButton : {pDecrease, pIncrease})
        {
            pButton->setProperty("settingsStepAction", true);
            pButton->setFixedSize(48, bMobile ? 48 : 40);
            pButton->setAccessibleName(pButton == pDecrease ? "减少一分钟" : "增加一分钟");
            connect(pButton, &QPushButton::clicked, this,
                    [this, pButton, pDecrease]
                    {
                        bool bValid = false;
                        const int nCurrent = m_pMinutes->text().toInt(&bValid);
                        const int nNext = qBound(1,
                                                 (bValid ? nCurrent : m_nCustomMinutes) +
                                                     (pButton == pDecrease ? -1 : 1),
                                                 ReadAloudController::maximumSleepMinutes());
                        m_pMinutes->setText(QString::number(nNext));
                    });
        }
        pDurationLayout->addWidget(pDecrease);
        pDurationLayout->addWidget(m_pMinutes, 1);
        pDurationLayout->addWidget(paragraph("分钟", "settingsHint"));
        pDurationLayout->addWidget(pIncrease);
        pCustomLayout->addWidget(pDuration);
        m_pHint = paragraph("支持 1–1440 分钟，确认后生效。", "settingsHint");
        pCustomLayout->addWidget(m_pHint);
        m_pApply = new QPushButton("开始定时");
        m_pApply->setObjectName("readAloudCustomApply");
        m_pApply->setProperty("primary", true);
        m_pApply->setMinimumHeight(bMobile ? 48 : 40);
        pCustomLayout->addWidget(m_pApply);
        pLayout->addWidget(m_pCustom);
        connect(m_pApply, &QPushButton::clicked, this, &SleepTimerControl::applyCustom);
        connect(m_pMinutes, &QLineEdit::returnPressed, this, &SleepTimerControl::applyCustom);
        connect(m_pMinutes, &QLineEdit::textChanged, this, &SleepTimerControl::updateInput);
        if (m_pController)
        {
            const int nDuration = m_pController->sleepDurationMinutes();
            if (nDuration > 0 && nDuration != 15 && nDuration != 30 && nDuration != 60)
            {
                m_nCustomMinutes = nDuration;
                m_pMinutes->setText(QString::number(nDuration));
                m_bEditing = true;
            }
            connect(m_pController, &ReadAloudController::changed, this,
                    &SleepTimerControl::updateState);
            connect(m_pController, &QObject::destroyed, this,
                    [this]
                    {
                        setEnabled(false);
                    });
        }
        else
        {
            setEnabled(false);
        }
        updateInput();
        updateState();
    }
    void SleepTimerControl::chooseMode(int nMode)
    {
        if (!m_pController)
        {
            return;
        }
        m_bEditing = nMode == -2;
        if (!m_bEditing)
        {
            m_pMinutes->clearFocus();
            QGuiApplication::inputMethod()->hide();
        }
        if (nMode == -1)
        {
            m_pController->setStopAtChapter(true);
        }
        else if (nMode >= 0)
        {
            m_pController->setSleepMinutes(nMode);
        }
        updateState();
        if (m_bEditing)
        {
            QTimer::singleShot(0, m_pMinutes,
                               [this]
                               {
                                   m_pMinutes->setFocus(Qt::OtherFocusReason);
                                   m_pMinutes->selectAll();
                               });
        }
    }
    void SleepTimerControl::updateInput()
    {
        const bool bValid = m_pMinutes->hasAcceptableInput();
        m_pApply->setEnabled(bValid && m_pController);
        m_pHint->setText(bValid ? "支持 1–1440 分钟，确认后生效。"
                                : "请输入 1–1440 之间的分钟数。");
    }
    void SleepTimerControl::applyCustom()
    {
        if (!m_pController || !m_pMinutes->hasAcceptableInput())
        {
            return;
        }
        m_nCustomMinutes = m_pMinutes->text().toInt();
        m_oSettings.setValue("speech/customSleepMinutes", m_nCustomMinutes);
        m_pMinutes->clearFocus();
        QGuiApplication::inputMethod()->hide();
        m_pController->setSleepMinutes(m_nCustomMinutes);
    }
    void SleepTimerControl::updateState()
    {
        if (!m_pController)
        {
            return;
        }
        const int nRemaining = m_pController->remainingSleepSeconds();
        const int nDuration = m_pController->sleepDurationMinutes();
        const bool bChapter = m_pController->stopAtChapter();
        int nSelected = 0;
        if (bChapter)
        {
            nSelected = -1;
            m_pRemaining->setText("将在本章结束时停止");
        }
        else if (nRemaining > 0)
        {
            nSelected = nDuration == 15 || nDuration == 30 || nDuration == 60 ? nDuration : -2;
            const QString strTime = nRemaining >= 3600
                                        ? QString("%1:%2:%3")
                                              .arg(nRemaining / 3600, 2, 10, QChar('0'))
                                              .arg(nRemaining / 60 % 60, 2, 10, QChar('0'))
                                              .arg(nRemaining % 60, 2, 10, QChar('0'))
                                        : QString("%1:%2")
                                              .arg(nRemaining / 60, 2, 10, QChar('0'))
                                              .arg(nRemaining % 60, 2, 10, QChar('0'));
            m_pRemaining->setText("剩余 " + strTime);
        }
        else
        {
            m_pRemaining->setText("未设置定时");
        }
        m_pCustom->setVisible(m_bEditing);
        for (auto* pChoice : m_vChoices)
        {
            pChoice->setChecked(pChoice->property("timerMinutes").toInt() ==
                                (m_bEditing ? -2 : nSelected));
        }
        m_pApply->setText(nRemaining > 0 || bChapter ? "重新定时" : "开始定时");
    }
} // namespace Wenku8::Reader
