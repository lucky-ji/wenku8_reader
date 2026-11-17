// SPDX-License-Identifier: GPL-2.0-only
#include "SpeechEngine.h"
#include <QPointer>
#include <QScopedValueRollback>
#include <QTextToSpeech>
#include <QTimer>
#include <QVector>
#include <QVoice>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
namespace Wenku8::Reader
{
    namespace
    {
        // Qt 5 TextToSpeech is used under its GPL-2.0-or-later option. Deploy its
        // system plugin (SAPI / macOS / speech-dispatcher), not a cloud speech SDK.
        class QtSpeechEngine final : public SpeechEngine
        {
          public:
            explicit QtSpeechEngine(QObject* pParent) : SpeechEngine(pParent)
            {
                m_oStartTimer.setSingleShot(true);
                connect(&m_oStartTimer, &QTimer::timeout, this,
                        [this]
                        {
                            startPending();
                        });
                m_oStartWatchdog.setSingleShot(true);
                m_oStartWatchdog.setInterval(8000);
                connect(&m_oStartWatchdog, &QTimer::timeout, this,
                        [this]
                        {
                            failCurrent("系统朗读引擎没有响应，请检查已安装的语音或稍后重试。");
                        });
                createBackend();
                if (auto* pApplication =
                        qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
                {
                    connect(pApplication, &QGuiApplication::applicationStateChanged, this,
                            [this](Qt::ApplicationState eState)
                            {
                                if (eState == Qt::ApplicationActive && m_bRefreshAfterSettings)
                                {
                                    m_bRefreshAfterSettings = false;
                                    stop();
                                    createBackend();
                                }
                            });
                }
            }
            ~QtSpeechEngine() override
            {
                if (m_pSpeech)
                {
                    m_pSpeech->disconnect(this);
                    m_pSpeech->stop();
                }
            }
            bool available() const override
            {
                return m_bAvailable;
            }
            QString errorString() const override
            {
                return m_strError;
            }
            bool canOpenSettings() const override
            {
#ifdef Q_OS_WIN
                return true;
#else
                return false;
#endif
            }
            bool openSettings() override
            {
#ifdef Q_OS_WIN
                m_bRefreshAfterSettings = QDesktopServices::openUrl(QUrl("ms-settings:speech"));
                return m_bRefreshAfterSettings;
#else
                return false;
#endif
            }
            QStringList voiceNames() const override
            {
                QStringList vNames;
                for (const auto& oVoice : m_vVoices)
                {
                    vNames.append(oVoice.m_strName);
                }
                return vNames;
            }
            QString voice() const override
            {
                return m_strVoice;
            }
            void setVoice(const QString& strVoice) override
            {
                if (strVoice == m_strVoice)
                {
                    return;
                }
                const auto oVoice = std::find_if(m_vVoices.cbegin(), m_vVoices.cend(),
                                                 [&strVoice](const VoiceChoice& oChoice)
                                                 {
                                                     return oChoice.m_strName == strVoice;
                                                 });
                if (oVoice == m_vVoices.cend())
                {
                    return;
                }
                m_strVoice = strVoice;
                if (!m_bActive && m_pSpeech && m_pSpeech->state() == QTextToSpeech::Ready)
                {
                    applySettings();
                }
            }
            void setRate(double fFactor) override
            {
                m_fRate = clampFactor(fFactor);
                if (m_pSpeech)
                {
                    m_pSpeech->setRate(std::log2(m_fRate));
                }
            }
            void setPitch(double fFactor) override
            {
                m_fPitch = clampFactor(fFactor);
                if (m_pSpeech)
                {
                    m_pSpeech->setPitch(std::log2(m_fPitch));
                }
            }
            void setVolume(double fVolume) override
            {
                m_fVolume = std::isfinite(fVolume) ? std::clamp(fVolume, 0.0, 1.0) : 1.0;
                if (m_pSpeech)
                {
                    m_pSpeech->setVolume(m_fVolume);
                }
            }
            void speak(const QString& strText, quint64 nUtteranceId) override
            {
                stop();
                m_strPendingText = strText.trimmed();
                m_nPendingId = nUtteranceId;
                m_bPending = true;
                m_oStartWatchdog.start();
                m_oStartTimer.start(0);
            }
            void stop() override
            {
                const bool bWasActive = m_bActive;
                m_bPending = false;
                m_bActive = false;
                m_bStarted = false;
                m_strPendingText.clear();
                m_oStartTimer.stop();
                m_oStartWatchdog.stop();
                if (!m_pSpeech)
                {
                    return;
                }
                if (bWasActive || m_pSpeech->state() == QTextToSpeech::Speaking ||
                    m_pSpeech->state() == QTextToSpeech::Paused)
                {
                    // Qt 5 completion callbacks have no utterance ID. Retire an
                    // interrupted backend before another sentence can be started.
                    createBackend();
                }
                else
                {
                    m_pSpeech->stop();
                }
            }

          private:
            struct VoiceChoice
            {
                QString m_strName;
                QLocale m_oLocale;
                QVoice m_oVoice;
            };
            QTextToSpeech* m_pSpeech = nullptr;
            QVector<VoiceChoice> m_vVoices;
            QString m_strVoice, m_strPendingText, m_strError;
            double m_fRate = 1.0, m_fPitch = 1.0, m_fVolume = 1.0;
            quint64 m_nBackendGeneration = 0, m_nPendingId = 0, m_nActiveId = 0;
            bool m_bAvailable = false, m_bInitialized = false, m_bConfiguring = false,
                 m_bPending = false, m_bActive = false, m_bStarted = false;
            bool m_bRefreshAfterSettings = false;
            QTimer m_oStartTimer, m_oStartWatchdog;
            static double clampFactor(double fFactor)
            {
                return std::isfinite(fFactor) ? std::clamp(fFactor, 0.5, 2.0) : 1.0;
            }
            void setAvailable(bool bAvailable, const QString& strError = QString())
            {
                if (m_bAvailable != bAvailable || m_strError != strError)
                {
                    m_bAvailable = bAvailable;
                    m_strError = strError;
                    emit readyChanged();
                }
            }
            void createBackend()
            {
                ++m_nBackendGeneration;
                if (m_pSpeech)
                {
                    m_pSpeech->disconnect(this);
                    m_pSpeech->stop();
                    m_pSpeech->deleteLater();
                    m_pSpeech = nullptr;
                }
                m_bInitialized = false;
                setAvailable(false, "正在初始化系统朗读引擎。");
                m_pSpeech = new QTextToSpeech(this);
                const quint64 nGeneration = m_nBackendGeneration;
                const QPointer<QTextToSpeech> pBackend = m_pSpeech;
                connect(m_pSpeech, &QTextToSpeech::stateChanged, this,
                        [this, pBackend, nGeneration](QTextToSpeech::State eState)
                        {
                            if (pBackend && pBackend == m_pSpeech &&
                                nGeneration == m_nBackendGeneration && !m_bConfiguring &&
                                pBackend->state() == eState)
                            {
                                handleState(eState);
                            }
                        });
                QTimer::singleShot(0, this,
                                   [this, pBackend, nGeneration]
                                   {
                                       if (pBackend && pBackend == m_pSpeech &&
                                           nGeneration == m_nBackendGeneration)
                                       {
                                           handleState(pBackend->state());
                                       }
                                   });
            }
            void initializeVoices()
            {
                if (!m_pSpeech || m_pSpeech->state() != QTextToSpeech::Ready)
                {
                    return;
                }
                m_bConfiguring = true;
                const QLocale oDefaultLocale = m_pSpeech->locale();
                const QVoice oDefaultVoice = m_pSpeech->voice();
                m_vVoices.clear();
                auto vLocales = m_pSpeech->availableLocales();
                if (vLocales.isEmpty())
                {
                    vLocales.append(oDefaultLocale);
                }
                QString strDefaultVoice;
                QString strChineseVoice;
                for (const auto& oLocale : vLocales)
                {
                    m_pSpeech->setLocale(oLocale);
                    for (const auto& oVoice : m_pSpeech->availableVoices())
                    {
                        const QString strLanguage =
                            oLocale.nativeLanguageName().isEmpty()
                                ? QLocale::languageToString(oLocale.language())
                                : oLocale.nativeLanguageName();
                        const QString strBase =
                            oVoice.name() + " · " + strLanguage + " (" + oLocale.name() + ")";
                        QString strName = strBase;
                        int nSuffix = 2;
                        while (std::any_of(m_vVoices.cbegin(), m_vVoices.cend(),
                                           [&strName](const VoiceChoice& oChoice)
                                           {
                                               return oChoice.m_strName == strName;
                                           }))
                        {
                            strName = strBase + " [" + QString::number(nSuffix++) + "]";
                        }
                        m_vVoices.append({strName, oLocale, oVoice});
                        if (oLocale.language() == QLocale::Chinese &&
                            (strChineseVoice.isEmpty() || oLocale.country() == QLocale::China))
                        {
                            strChineseVoice = strName;
                        }
                        if (oLocale == oDefaultLocale && oVoice.name() == oDefaultVoice.name())
                        {
                            strDefaultVoice = strName;
                        }
                    }
                }
                m_pSpeech->setLocale(oDefaultLocale);
                m_pSpeech->setVoice(oDefaultVoice);
                if (std::none_of(m_vVoices.cbegin(), m_vVoices.cend(),
                                 [this](const VoiceChoice& oChoice)
                                 {
                                     return oChoice.m_strName == m_strVoice;
                                 }))
                {
                    m_strVoice = !strChineseVoice.isEmpty() ? strChineseVoice
                                 : strDefaultVoice.isEmpty() && !m_vVoices.isEmpty()
                                     ? m_vVoices.front().m_strName
                                     : strDefaultVoice;
                }
                m_bInitialized = true;
                applySettings();
                m_bConfiguring = false;
                const bool bAvailable =
                    !m_vVoices.isEmpty() && m_pSpeech->state() != QTextToSpeech::BackendError;
                setAvailable(
                    bAvailable,
                    bAvailable ? QString()
                               : QStringLiteral("系统没有可用的朗读语音，请安装或启用系统语音。"));
                if (m_bPending)
                {
                    m_oStartTimer.start(0);
                }
            }
            void applySettings()
            {
                if (!m_pSpeech)
                {
                    return;
                }
                QScopedValueRollback<bool> oGuard(m_bConfiguring, true);
                for (const auto& oChoice : m_vVoices)
                {
                    if (oChoice.m_strName == m_strVoice)
                    {
                        m_pSpeech->setLocale(oChoice.m_oLocale);
                        m_pSpeech->setVoice(oChoice.m_oVoice);
                        break;
                    }
                }
                m_pSpeech->setRate(std::log2(m_fRate));
                m_pSpeech->setPitch(std::log2(m_fPitch));
                m_pSpeech->setVolume(m_fVolume);
            }
            void handleState(QTextToSpeech::State eState)
            {
                if (eState == QTextToSpeech::BackendError)
                {
                    const QString strError =
                        "系统朗读引擎不可用，请检查已安装的语音与系统朗读服务。";
                    setAvailable(false, strError);
                    failCurrent(strError);
                    return;
                }
                if (!m_bInitialized && eState == QTextToSpeech::Ready)
                {
                    initializeVoices();
                    return;
                }
                if (eState == QTextToSpeech::Speaking && m_bActive && !m_bStarted)
                {
                    m_bStarted = true;
                    m_oStartWatchdog.stop();
                    emit started(m_nActiveId);
                }
                else if (eState == QTextToSpeech::Ready && m_bActive && m_bStarted)
                {
                    const quint64 nId = m_nActiveId;
                    m_bActive = false;
                    m_bStarted = false;
                    emit finished(nId);
                }
                else if (eState == QTextToSpeech::Paused && m_bActive)
                {
                    stop();
                    emit interrupted("朗读已被系统暂停。");
                }
                if (m_bPending && !m_bActive && eState == QTextToSpeech::Ready)
                {
                    m_oStartTimer.start(0);
                }
            }
            void startPending()
            {
                if (!m_bPending || m_bActive)
                {
                    return;
                }
                if (auto* pApplication =
                        qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
                {
                    if (pApplication->applicationState() != Qt::ApplicationActive)
                    {
                        stop();
                        emit interrupted("应用已进入后台，听书已暂停");
                        return;
                    }
                }
                if (m_strPendingText.isEmpty())
                {
                    failCurrent("没有可朗读的内容。");
                    return;
                }
                if (!m_pSpeech || m_pSpeech->state() == QTextToSpeech::BackendError ||
                    (m_bInitialized && !m_bAvailable))
                {
                    failCurrent("系统朗读引擎不可用，请安装或启用本地语音。");
                    return;
                }
                if (!m_bAvailable || m_pSpeech->state() != QTextToSpeech::Ready)
                {
                    return;
                }
                applySettings();
                if (m_pSpeech->state() == QTextToSpeech::BackendError)
                {
                    handleState(QTextToSpeech::BackendError);
                    return;
                }
                const QString strText = m_strPendingText;
                m_nActiveId = m_nPendingId;
                m_bPending = false;
                m_strPendingText.clear();
                m_bActive = true;
                m_bStarted = false;
                m_oStartWatchdog.start();
                m_pSpeech->say(strText);
            }
            void failCurrent(const QString& strMessage)
            {
                if (!m_bActive && !m_bPending)
                {
                    return;
                }
                const quint64 nId = m_bActive ? m_nActiveId : m_nPendingId;
                stop();
                emit failed(nId, strMessage);
            }
        };
    } // namespace
    SpeechEngine* createSpeechEngine(QObject* pParent)
    {
        return new QtSpeechEngine(pParent);
    }
} // namespace Wenku8::Reader
