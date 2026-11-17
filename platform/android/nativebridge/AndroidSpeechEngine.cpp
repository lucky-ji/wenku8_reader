// SPDX-License-Identifier: GPL-2.0-only
#include "SpeechEngine.h"
#include <QAndroidJniEnvironment>
#include <QAndroidJniObject>
#include <QtAndroid>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <cmath>
#include <jni.h>

namespace Wenku8::Reader
{
    namespace
    {
        class AndroidSpeechEngine;
        QHash<quint64, QPointer<AndroidSpeechEngine>> oSpeechEngines;
        quint64 nNextSpeechOwner = 0;
        constexpr auto pSpeechClass = "io/github/lucky_ji/wenku8_reader/SpeechPlayer";

        bool clearJniError()
        {
            QAndroidJniEnvironment oEnvironment;
            if (oEnvironment->ExceptionCheck())
            {
                oEnvironment->ExceptionClear();
                return true;
            }
            return false;
        }

        class AndroidSpeechEngine final : public SpeechEngine
        {
          public:
            explicit AndroidSpeechEngine(QObject* pParent)
                : SpeechEngine(pParent)
            {
                connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                        [this](Qt::ApplicationState eState)
                {
                    if (eState == Qt::ApplicationActive && m_bRefreshAfterSettings)
                    {
                        m_bRefreshAfterSettings = false;
                        recreate();
                    }
                });
                create();
            }

            ~AndroidSpeechEngine() override
            {
                oSpeechEngines.remove(m_nOwner);
                if (m_bCreated)
                {
                    QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "destroy", "(J)V", jlong(m_nOwner));
                    clearJniError();
                }
            }

            bool available() const override
            {
                return m_bAvailable;
            }
            QStringList voiceNames() const override
            {
                return m_vVoices;
            }
            QString voice() const override
            {
                return m_strVoice;
            }
            bool requiresNetwork() const override
            {
                return m_bNetwork;
            }
            QString errorString() const override
            {
                return m_strError;
            }
            void setVoice(const QString& strVoice) override
            {
                m_strPreferredVoice = strVoice;
                if (!m_bCreated || strVoice == m_strVoice)
                {
                    return;
                }
                stop();
                ++m_nVoiceRevision;
                m_bAvailable = false;
                m_bNetwork = true;
                m_strError = QStringLiteral("正在切换系统音色…");
                const auto oVoice = QAndroidJniObject::fromString(strVoice);
                QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "setVoice", "(JLjava/lang/String;J)V",
                    jlong(m_nOwner), oVoice.object<jstring>(), jlong(m_nVoiceRevision));
                clearJniError();
                emit readyChanged();
            }
            void setRate(double fRate) override
            {
                m_fRate = normalized(fRate, 0.5, 2.0);
                setNumber("setRate", m_fRate);
            }
            void setPitch(double fPitch) override
            {
                m_fPitch = normalized(fPitch, 0.5, 2.0);
                setNumber("setPitch", m_fPitch);
            }
            void setVolume(double fVolume) override
            {
                m_fVolume = normalized(fVolume, 0.0, 1.0);
                setNumber("setVolume", m_fVolume);
            }
            void speak(const QString& strText, quint64 nToken) override
            {
                if (QGuiApplication::applicationState() != Qt::ApplicationActive)
                {
                    stop();
                    emit interrupted(QStringLiteral("应用已进入后台，朗读已暂停。"));
                    return;
                }
                if (!m_bCreated || !m_bAvailable)
                {
                    QTimer::singleShot(0, this, [this, nToken]
                    {
                        emit failed(nToken, m_strError);
                    });
                    return;
                }
                m_nUtterance = nToken;
                m_bSpeaking = true;
                const auto oText = QAndroidJniObject::fromString(strText);
                QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "speak", "(JLjava/lang/String;J)V",
                    jlong(m_nOwner), oText.object<jstring>(), jlong(nToken));
                if (clearJniError())
                {
                    m_bSpeaking = false;
                    emit failed(nToken, QStringLiteral("系统语音无法开始朗读，请检查文字转语音设置。"));
                }
            }
            void stop() override
            {
                m_bSpeaking = false;
                if (m_bCreated)
                {
                    QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "stop", "(J)V", jlong(m_nOwner));
                    clearJniError();
                }
            }
            bool openSettings() override
            {
                const auto oActivity = QtAndroid::androidActivity();
                if (!oActivity.isValid() || !QAndroidJniObject::isClassAvailable(pSpeechClass))
                {
                    return false;
                }
                const bool bOpened = QAndroidJniObject::callStaticMethod<jboolean>(pSpeechClass,
                    "openSettings", "(Landroid/app/Activity;)Z", oActivity.object<jobject>());
                if (!clearJniError() && bOpened)
                {
                    m_bRefreshAfterSettings = true;
                    return true;
                }
                return false;
            }
            bool canOpenSettings() const override
            {
                const auto oActivity = QtAndroid::androidActivity();
                if (!oActivity.isValid() || !QAndroidJniObject::isClassAvailable(pSpeechClass))
                {
                    return false;
                }
                const bool bAvailable = QAndroidJniObject::callStaticMethod<jboolean>(pSpeechClass,
                    "canOpenSettings", "(Landroid/app/Activity;)Z", oActivity.object<jobject>());
                return !clearJniError() && bAvailable;
            }
            void event(int nEvent, quint64 nToken, const QString& strData)
            {
                if (nEvent == 0)
                {
                    const auto oData = QJsonDocument::fromJson(strData.toUtf8()).object();
                    if (oData.value("voiceRevision").toInt() != m_nVoiceRevision)
                    {
                        return;
                    }
                    m_bAvailable = oData.value("available").toBool();
                    m_bNetwork = oData.value("network").toBool(true);
                    m_strVoice = oData.value("voice").toString();
                    m_strError = oData.value("error").toString();
                    m_vVoices.clear();
                    for (const auto& oName : oData.value("voices").toArray())
                    {
                        m_vVoices.append(oName.toString());
                    }
                    emit readyChanged();
                    return;
                }
                if (nEvent == 4)
                {
                    if (nToken != m_nUtterance)
                    {
                        return;
                    }
                    m_bSpeaking = false;
                    emit interrupted(strData);
                    return;
                }
                if (nEvent == 3 && !m_bSpeaking && nToken == 0)
                {
                    m_bAvailable = false;
                    m_strError = strData;
                    emit readyChanged();
                    return;
                }
                if (!m_bSpeaking || nToken != m_nUtterance)
                {
                    return;
                }
                if (nEvent == 1)
                {
                    emit started(nToken);
                }
                else if (nEvent == 2)
                {
                    m_bSpeaking = false;
                    emit finished(nToken);
                }
                else if (nEvent == 3)
                {
                    m_bSpeaking = false;
                    emit failed(nToken, strData);
                }
            }

          private:
            void create()
            {
                m_nOwner = ++nNextSpeechOwner;
                oSpeechEngines.insert(m_nOwner, this);
                const auto oActivity = QtAndroid::androidActivity();
                if (!oActivity.isValid() || !QAndroidJniObject::isClassAvailable(pSpeechClass))
                {
                    m_strError = QStringLiteral("系统语音暂不可用，请返回阅读页后重试。");
                    QTimer::singleShot(0, this, [this]
                    {
                        emit readyChanged();
                    });
                    return;
                }
                QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "create",
                    "(Landroid/app/Activity;J)V", oActivity.object<jobject>(), jlong(m_nOwner));
                if (clearJniError())
                {
                    m_strError = QStringLiteral("无法初始化系统语音，请检查文字转语音设置。");
                    QTimer::singleShot(0, this, [this]
                    {
                        emit readyChanged();
                    });
                    return;
                }
                m_bCreated = true;
                setNumber("setRate", m_fRate);
                setNumber("setPitch", m_fPitch);
                setNumber("setVolume", m_fVolume);
                if (!m_strPreferredVoice.isEmpty())
                {
                    setVoice(m_strPreferredVoice);
                }
            }
            void recreate()
            {
                stop();
                oSpeechEngines.remove(m_nOwner);
                if (m_bCreated)
                {
                    QAndroidJniObject::callStaticMethod<void>(pSpeechClass, "destroy", "(J)V", jlong(m_nOwner));
                    clearJniError();
                }
                m_bCreated = false;
                m_bAvailable = false;
                m_bNetwork = true;
                m_nVoiceRevision = 0;
                m_nUtterance = 0;
                m_vVoices.clear();
                m_strVoice.clear();
                m_strError = QStringLiteral("正在重新初始化系统语音引擎…");
                create();
                emit readyChanged();
            }
            static double normalized(double fValue, double fMinimum, double fMaximum)
            {
                return std::isfinite(fValue) ? qBound(fMinimum, fValue, fMaximum) : 1.0;
            }
            void setNumber(const char* pMethod, double fValue)
            {
                if (!m_bCreated)
                {
                    return;
                }
                QAndroidJniObject::callStaticMethod<void>(pSpeechClass, pMethod, "(JD)V",
                                                         jlong(m_nOwner), jdouble(fValue));
                clearJniError();
            }
            quint64 m_nOwner = 0;
            quint64 m_nUtterance = 0;
            int m_nVoiceRevision = 0;
            bool m_bCreated = false;
            bool m_bAvailable = false;
            bool m_bNetwork = true;
            bool m_bSpeaking = false;
            bool m_bRefreshAfterSettings = false;
            double m_fRate = 1.0, m_fPitch = 1.0, m_fVolume = 1.0;
            QStringList m_vVoices;
            QString m_strVoice;
            QString m_strPreferredVoice;
            QString m_strError = QStringLiteral("正在初始化系统语音引擎…");
        };
    }

    SpeechEngine* createSpeechEngine(QObject* pParent)
    {
        return new AndroidSpeechEngine(pParent);
    }

    void dispatchAndroidSpeechEvent(quint64 nOwner, int nEvent, quint64 nToken, const QString& strData)
    {
        if (!QCoreApplication::instance())
        {
            return;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [nOwner, nEvent, nToken, strData]
        {
            const auto pEngine = oSpeechEngines.value(nOwner);
            if (pEngine)
            {
                pEngine->event(nEvent, nToken, strData);
            }
        }, Qt::QueuedConnection);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_lucky_1ji_wenku8_1reader_SpeechPlayer_nativeEvent(
    JNIEnv* pEnvironment, jclass, jlong nOwner, jint nEvent, jlong nToken, jstring pData)
{
    QString strData;
    if (pData)
    {
        const auto* pCharacters = pEnvironment->GetStringChars(pData, nullptr);
        if (pCharacters)
        {
            strData = QString::fromUtf16(reinterpret_cast<const ushort*>(pCharacters),
                                        pEnvironment->GetStringLength(pData));
            pEnvironment->ReleaseStringChars(pData, pCharacters);
        }
    }
    Wenku8::Reader::dispatchAndroidSpeechEvent(quint64(nOwner), int(nEvent), quint64(nToken), strData);
}
