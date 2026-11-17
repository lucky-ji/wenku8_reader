// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QPointer>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
namespace Wenku8::Reader
{
    class ReadAloudController;
    class ReadAloudPanel final : public QWidget
    {
        Q_OBJECT
      public:
        explicit ReadAloudPanel(ReadAloudController* pController, bool bMobile,
                                QWidget* pParent = nullptr);
        void setChapterTitle(const QString& strTitle);
      signals:
        void seekReadingPosition();
      private:
        QPointer<ReadAloudController> m_pController;
        bool m_bMobile = false, m_bVoiceRefreshPending = false, m_bPauseButton = false;
        int m_nSleepPreset = 0;
        QString m_strLocalStatus;
        QLabel* m_pChapter = nullptr;
        QLabel* m_pPreview = nullptr;
        QLabel* m_pProgressText = nullptr;
        QLabel* m_pStatus = nullptr;
        QLabel* m_pSleepRemaining = nullptr;
        QLabel* m_pNetworkHint = nullptr;
        QLabel* m_pRateValue = nullptr;
        QLabel* m_pPitchValue = nullptr;
        QLabel* m_pVolumeValue = nullptr;
        QProgressBar* m_pProgress = nullptr;
        QPushButton* m_pPrevious = nullptr;
        QPushButton* m_pToggle = nullptr;
        QPushButton* m_pNext = nullptr;
        QPushButton* m_pStop = nullptr;
        QPushButton* m_pFromReading = nullptr;
        QPushButton* m_pEngineSettings = nullptr;
        QCheckBox* m_pFollow = nullptr;
        QCheckBox* m_pAutoNext = nullptr;
        QCheckBox* m_pNetworkConsent = nullptr;
        QComboBox* m_pSleep = nullptr;
        QComboBox* m_pVoice = nullptr;
        QSlider* m_pRate = nullptr;
        QSlider* m_pPitch = nullptr;
        QSlider* m_pVolume = nullptr;
        void updateState();
        void refreshVoices();
    };
}
