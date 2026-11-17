// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "SpeechEngine.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>
namespace Wenku8::Reader
{
    struct SpeechSegment
    {
        QString m_strText;
        int m_nStart = 0, m_nEnd = 0;
    };
    QVector<SpeechSegment> splitSpeechText(const QString& strText);
    class ReadAloudController final : public QObject
    {
        Q_OBJECT
      public:
        enum class State
        {
            Stopped,
            Playing,
            Paused,
            WaitingChapter,
            Error
        };
        explicit ReadAloudController(SpeechEngine* pEngine, QObject* pParent = nullptr);
        ~ReadAloudController() override;
        void loadChapter(int nBook, int nChapter, const QString& strText, int nOffset,
                         bool bResume = false);
        bool canResume(int nBook) const;
        void play();
        void pause(const QString& strReason = {});
        void stop();
        void step(int nDirection);
        void seekOffset(int nOffset);
        void setFollow(bool bValue);
        bool follow() const
        {
            return m_bFollow;
        }
        void setAutoNext(bool bValue);
        bool autoNext() const
        {
            return m_bAutoNext;
        }
        void setSleepMinutes(int nMinutes);
        void setStopAtChapter(bool bValue);
        bool stopAtChapter() const
        {
            return m_bStopAtChapter;
        }
        int remainingSleepSeconds() const;
        void setVoice(const QString& strVoice);
        void setRate(double fValue);
        void setPitch(double fValue);
        void setVolume(double fValue);
        double rate() const
        {
            return m_fRate;
        }
        double pitch() const
        {
            return m_fPitch;
        }
        double volume() const
        {
            return m_fVolume;
        }
        void setAllowNetworkVoice(bool bValue);
        bool allowNetworkVoice() const
        {
            return m_bAllowNetworkVoice;
        }
        State state() const
        {
            return m_eState;
        }
        int index() const
        {
            return m_nIndex;
        }
        int count() const
        {
            return m_vSegments.size();
        }
        int bookId() const
        {
            return m_nBook;
        }
        int chapterId() const
        {
            return m_nChapter;
        }
        int startOffset() const;
        int endOffset() const;
        QString currentText() const;
        QString status() const
        {
            return m_strStatus;
        }
        SpeechEngine* engine() const
        {
            return m_pEngine;
        }
      signals:
        void changed();
        void rangeChanged(int nStart, int nEnd, bool bFollow);
        void chapterRequested(int nDirection);
        void notice(const QString& strMessage);

      private:
        QPointer<SpeechEngine> m_pEngine;
        QVector<SpeechSegment> m_vSegments;
        State m_eState = State::Stopped;
        int m_nBook = 0, m_nChapter = 0, m_nIndex = 0;
        quint64 m_nToken = 0;
        bool m_bFollow = true, m_bAutoNext = true, m_bStopAtChapter = false;
        bool m_bAllowNetworkVoice = false;
        bool m_bUtterancePending = false, m_bUtteranceStarted = false, m_bCancelling = false;
        bool m_bChapterPending = false, m_bResumeAfterChapter = false;
        double m_fRate = 1, m_fPitch = 1, m_fVolume = 1;
        QString m_strStatus;
        QTimer m_oWatchdog, m_oSleepTimer, m_oSleepTick;
        QElapsedTimer m_oSleepElapsed;
        int m_nSleepDuration = 0;
        void cancelUtterance();
        void speakCurrent();
        void setState(State eState, const QString& strStatus);
        void resetSleep();
        void clearChapterRequest();
        int indexForOffset(int nOffset) const;
        bool ensureVoiceReady();
    };
} // namespace Wenku8::Reader
