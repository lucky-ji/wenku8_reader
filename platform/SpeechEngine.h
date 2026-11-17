// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QObject>
#include <QStringList>
namespace Wenku8::Reader
{
    // Platform-owned system speech service. Calls and signals use its QObject thread.
    // Cancelling an utterance never emits finished(); IDs belong to the caller.
    class SpeechEngine : public QObject
    {
        Q_OBJECT
      public:
        explicit SpeechEngine(QObject* pParent = nullptr) : QObject(pParent)
        {
        }
        ~SpeechEngine() override = default;
        virtual bool available() const = 0;
        virtual QStringList voiceNames() const = 0;
        virtual QString voice() const = 0;
        virtual void setVoice(const QString& strVoice) = 0;
        virtual void setRate(double fFactor) = 0;
        virtual void setPitch(double fFactor) = 0;
        virtual void setVolume(double fVolume) = 0;
        virtual void speak(const QString& strText, quint64 nUtteranceId) = 0;
        virtual void stop() = 0;
        virtual bool requiresNetwork() const
        {
            return false;
        }
        virtual QString errorString() const
        {
            return {};
        }
        virtual bool canOpenSettings() const
        {
            return false;
        }
        virtual bool openSettings()
        {
            return false;
        }
      signals:
        void readyChanged();
        void started(quint64 nUtteranceId);
        void finished(quint64 nUtteranceId);
        void failed(quint64 nUtteranceId, const QString& strMessage);
        void interrupted(const QString& strReason);
        void playbackAction(const QString& strAction);
    };
    // The caller owns the returned object through pParent, or deletes it explicitly.
    SpeechEngine* createSpeechEngine(QObject* pParent = nullptr);
}
