// SPDX-License-Identifier: GPL-2.0-only
#include "ReadAloudController.h"
#include <QtMath>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
namespace Wenku8::Reader
{
    QVector<SpeechSegment> splitSpeechText(const QString& strText)
    {
        QVector<SpeechSegment> vResult;
        const QString strSentenceEnds = QString::fromUtf8("。！？!?；;");
        const QString strClosingMarks = QString::fromUtf8("”’\"」』）)");
        int nStart = 0;
        while (nStart < strText.size())
        {
            while (
                nStart < strText.size() &&
                (strText[nStart].isSpace() || strText[nStart] == QChar::ObjectReplacementCharacter))
                ++nStart;
            if (nStart >= strText.size())
                break;
            int nEnd = nStart;
            const int nLimit = qMin(strText.size(), nStart + 180);
            while (nEnd < nLimit)
            {
                const QChar oChar = strText[nEnd];
                if (oChar == '\n' || oChar == '\r' || oChar == QChar::ParagraphSeparator ||
                    oChar == QChar::LineSeparator || oChar == QChar::ObjectReplacementCharacter)
                    break;
                const bool bPair = oChar.isHighSurrogate() && nEnd + 1 < strText.size() &&
                                   strText[nEnd + 1].isLowSurrogate();
                if (bPair && nEnd + 2 > nLimit)
                    break;
                ++nEnd;
                if (bPair)
                    ++nEnd;
                if (strSentenceEnds.contains(oChar))
                {
                    while (nEnd < nLimit && strClosingMarks.contains(strText[nEnd]))
                        ++nEnd;
                    break;
                }
            }
            if (nEnd == nStart)
            {
                ++nStart;
                continue;
            }
            int nTrimmedEnd = nEnd;
            while (nTrimmedEnd > nStart && strText[nTrimmedEnd - 1].isSpace())
                --nTrimmedEnd;
            const QString strPart = strText.mid(nStart, nTrimmedEnd - nStart);
            if (!strPart.isEmpty())
                vResult.append({strPart, nStart, nTrimmedEnd});
            nStart = nEnd;
        }
        return vResult;
    }
    ReadAloudController::ReadAloudController(SpeechEngine* pEngine, QObject* pParent)
        : QObject(pParent), m_pEngine(pEngine)
    {
        Q_ASSERT(m_pEngine);
        if (!m_pEngine->parent())
            m_pEngine->setParent(this);
        m_oWatchdog.setSingleShot(true);
        m_oSleepTimer.setSingleShot(true);
        m_oSleepTick.setInterval(1000);
        connect(&m_oSleepTick, &QTimer::timeout, this, &ReadAloudController::changed);
        connect(&m_oSleepTimer, &QTimer::timeout, this,
                [this]
                {
                    if (!m_oSleepElapsed.isValid() || m_nSleepDuration <= 0)
                        return;
                    const qint64 nRemaining = m_nSleepDuration - m_oSleepElapsed.elapsed();
                    if (nRemaining > 0)
                    {
                        m_oSleepTimer.start(int(nRemaining));
                        return;
                    }
                    stop();
                    m_strStatus = "睡眠定时已结束";
                    emit notice(m_strStatus);
                    emit changed();
                });
        connect(&m_oWatchdog, &QTimer::timeout, this,
                [this]
                {
                    if (m_eState != State::Playing || !m_bUtterancePending)
                        return;
                    cancelUtterance();
                    clearChapterRequest();
                    setState(State::Error, "系统朗读长时间未响应，已暂停，可重新播放或更换音色。");
                    emit rangeChanged(-1, -1, false);
                });
        connect(m_pEngine, &SpeechEngine::started, this,
                [this](quint64 nId)
                {
                    if (nId != m_nToken || m_eState != State::Playing || !m_bUtterancePending ||
                        m_bUtteranceStarted)
                        return;
                    m_bUtteranceStarted = true;
                    m_oWatchdog.start(
                        qBound(30000, int(currentText().size() * 500 / m_fRate + 15000), 180000));
                    m_strStatus = "正在朗读";
                    emit rangeChanged(startOffset(), endOffset(), m_bFollow);
                    emit changed();
                });
        connect(m_pEngine, &SpeechEngine::finished, this,
                [this](quint64 nId)
                {
                    if (nId != m_nToken || m_eState != State::Playing || !m_bUtterancePending ||
                        !m_bUtteranceStarted)
                        return;
                    m_oWatchdog.stop();
                    m_bUtterancePending = false;
                    m_bUtteranceStarted = false;
                    ++m_nToken;
                    if (m_nIndex + 1 < m_vSegments.size())
                    {
                        ++m_nIndex;
                        speakCurrent();
                    }
                    else if (m_bStopAtChapter || !m_bAutoNext)
                    {
                        const bool bTimed = m_bStopAtChapter;
                        stop();
                        setState(State::Stopped, bTimed ? "已在本章结束处停止" : "本章朗读完成");
                    }
                    else
                    {
                        m_bChapterPending = true;
                        m_bResumeAfterChapter = true;
                        setState(State::WaitingChapter, "正在载入下一章");
                        if (m_eState == State::WaitingChapter && m_bChapterPending)
                            emit chapterRequested(1);
                    }
                });
        connect(m_pEngine, &SpeechEngine::failed, this,
                [this](quint64 nId, const QString& strMessage)
                {
                    if (nId != m_nToken || m_eState != State::Playing || !m_bUtterancePending)
                        return;
                    cancelUtterance();
                    clearChapterRequest();
                    setState(State::Error,
                             strMessage.isEmpty() ? "朗读失败，请检查系统语音引擎。" : strMessage);
                    emit rangeChanged(-1, -1, false);
                });
        connect(m_pEngine, &SpeechEngine::interrupted, this,
                [this](const QString& strReason)
                {
                    if (!m_bCancelling &&
                        (m_eState == State::Playing || m_eState == State::WaitingChapter))
                        pause(strReason);
                });
        connect(m_pEngine, &SpeechEngine::readyChanged, this,
                [this]
                {
                    if (!m_bCancelling && m_eState == State::Playing &&
                        (!m_pEngine->available() ||
                         (m_pEngine->requiresNetwork() && !m_bAllowNetworkVoice)))
                        pause("系统音色已变化，请确认音色后重新播放。");
                    else
                        emit changed();
                });
        m_oSleepTimer.setTimerType(Qt::PreciseTimer);
    }
    ReadAloudController::~ReadAloudController()
    {
        cancelUtterance();
    }
    void ReadAloudController::setState(State eState, const QString& strStatus)
    {
        m_eState = eState;
        m_strStatus = strStatus;
        emit changed();
    }
    void ReadAloudController::cancelUtterance()
    {
        ++m_nToken;
        m_bUtterancePending = false;
        m_bUtteranceStarted = false;
        m_oWatchdog.stop();
        if (m_pEngine && !m_bCancelling)
        {
            const QScopedValueRollback<bool> oGuard(m_bCancelling, true);
            m_pEngine->stop();
        }
    }
    void ReadAloudController::resetSleep()
    {
        m_oSleepTimer.stop();
        m_oSleepTick.stop();
        m_oSleepElapsed.invalidate();
        m_nSleepDuration = 0;
        m_bStopAtChapter = false;
    }
    void ReadAloudController::loadChapter(int nBook, int nChapter, const QString& strText,
                                          int nOffset, bool bResume)
    {
        if (bResume &&
            (!m_bChapterPending || m_eState != State::WaitingChapter || m_nBook != nBook))
            return;
        const bool bContinue = bResume && m_bChapterPending && m_nBook == nBook;
        const bool bPlay = bContinue && m_bResumeAfterChapter;
        cancelUtterance();
        clearChapterRequest();
        if (!bContinue)
            resetSleep();
        m_nBook = nBook;
        m_nChapter = nChapter;
        m_vSegments = splitSpeechText(strText);
        m_nIndex = indexForOffset(nOffset);
        const quint64 nLoadToken = m_nToken;
        emit rangeChanged(-1, -1, false);
        if (m_nToken != nLoadToken)
            return;
        setState(State::Stopped, m_vSegments.isEmpty() ? "本章没有可朗读的正文" : "准备朗读");
        if (bPlay && m_nToken == nLoadToken && m_nBook == nBook && m_nChapter == nChapter &&
            m_eState == State::Stopped)
            play();
    }
    void ReadAloudController::speakCurrent()
    {
        if (m_vSegments.isEmpty())
            return;
        const auto nId = ++m_nToken;
        m_bUtterancePending = true;
        m_bUtteranceStarted = false;
        m_eState = State::Playing;
        m_strStatus = "正在准备语音";
        emit changed();
        QTimer::singleShot(0, this,
                           [this, nId]
                           {
                               if (m_eState != State::Playing || nId != m_nToken ||
                                   !m_bUtterancePending)
                                   return;
                               if (!ensureVoiceReady())
                               {
                                   cancelUtterance();
                                   return;
                               }
                               m_oWatchdog.start(15000);
                               m_pEngine->speak(currentText(), nId);
                           });
    }
    void ReadAloudController::play()
    {
        if (m_eState == State::WaitingChapter && m_bChapterPending)
        {
            m_bResumeAfterChapter = true;
            setState(State::WaitingChapter, "正在等待章节载入，完成后继续朗读");
            return;
        }
        if (m_vSegments.isEmpty())
        {
            setState(State::Stopped, "本章没有可朗读的正文");
            return;
        }
        cancelUtterance();
        clearChapterRequest();
        if (!ensureVoiceReady())
            return;
        const quint64 nPlayToken = m_nToken;
        m_pEngine->setRate(m_fRate);
        m_pEngine->setPitch(m_fPitch);
        m_pEngine->setVolume(m_fVolume);
        if (m_nToken == nPlayToken)
            speakCurrent();
    }
    void ReadAloudController::pause(const QString& strReason)
    {
        cancelUtterance();
        clearChapterRequest();
        setState(State::Paused, strReason.isEmpty() ? "已暂停，继续时从当前短段重读" : strReason);
    }
    void ReadAloudController::stop()
    {
        cancelUtterance();
        clearChapterRequest();
        resetSleep();
        setState(State::Stopped, "朗读已停止");
        emit rangeChanged(-1, -1, false);
    }
    void ReadAloudController::step(int nDirection)
    {
        if (m_vSegments.isEmpty() || nDirection == 0)
            return;
        const bool bPlaying = m_eState == State::Playing;
        const int nTarget = m_nIndex + (nDirection < 0 ? -1 : 1);
        cancelUtterance();
        clearChapterRequest();
        if (nTarget < 0 || nTarget >= m_vSegments.size())
        {
            if (m_bAutoNext)
            {
                m_bChapterPending = true;
                m_bResumeAfterChapter = bPlaying;
                setState(State::WaitingChapter,
                         nDirection < 0 ? "正在载入上一章" : "正在载入下一章");
                if (m_eState == State::WaitingChapter && m_bChapterPending)
                    emit chapterRequested(nDirection < 0 ? -1 : 1);
            }
            else
                setState(State::Paused, "已到本章边界");
            return;
        }
        m_nIndex = nTarget;
        const quint64 nStepToken = m_nToken;
        setState(State::Paused, "已选择朗读段落");
        if (m_nToken != nStepToken)
            return;
        emit rangeChanged(startOffset(), endOffset(), m_bFollow);
        if (bPlaying && m_nToken == nStepToken && m_eState == State::Paused)
            play();
    }
    void ReadAloudController::seekOffset(int nOffset)
    {
        const bool bPlaying = m_eState == State::Playing;
        cancelUtterance();
        clearChapterRequest();
        m_nIndex = indexForOffset(nOffset);
        const quint64 nSeekToken = m_nToken;
        setState(State::Paused, "已定位到当前阅读段落");
        if (m_nToken != nSeekToken)
            return;
        emit rangeChanged(startOffset(), endOffset(), m_bFollow);
        if (bPlaying && m_nToken == nSeekToken && m_eState == State::Paused)
            play();
    }
    void ReadAloudController::setFollow(bool bValue)
    {
        m_bFollow = bValue;
        if (m_eState == State::Playing || m_eState == State::Paused)
            emit rangeChanged(startOffset(), endOffset(), m_bFollow);
        emit changed();
    }
    void ReadAloudController::setAutoNext(bool bValue)
    {
        m_bAutoNext = bValue;
        emit changed();
    }
    void ReadAloudController::setSleepMinutes(int nMinutes)
    {
        resetSleep();
        if (nMinutes > 0)
        {
            m_nSleepDuration = qMin(maximumSleepMinutes(), nMinutes) * 60000;
            m_oSleepElapsed.start();
            m_oSleepTimer.start(m_nSleepDuration);
            m_oSleepTick.start();
        }
        emit changed();
    }
    void ReadAloudController::setStopAtChapter(bool bValue)
    {
        resetSleep();
        m_bStopAtChapter = bValue;
        emit changed();
    }
    int ReadAloudController::remainingSleepSeconds() const
    {
        return m_nSleepDuration > 0 && m_oSleepElapsed.isValid()
                   ? qMax(0, int((m_nSleepDuration - m_oSleepElapsed.elapsed() + 999) / 1000))
                   : 0;
    }
    void ReadAloudController::setVoice(const QString& strVoice)
    {
        cancelUtterance();
        clearChapterRequest();
        setState(State::Paused, "已切换系统音色，确认音色就绪后点击播放");
        if (m_pEngine)
            m_pEngine->setVoice(strVoice);
    }
    void ReadAloudController::setRate(double fValue)
    {
        m_fRate = std::isfinite(fValue) ? qBound(.5, fValue, 2.0) : 1.0;
        if (m_pEngine)
            m_pEngine->setRate(m_fRate);
        emit changed();
    }
    void ReadAloudController::setPitch(double fValue)
    {
        m_fPitch = std::isfinite(fValue) ? qBound(.5, fValue, 2.0) : 1.0;
        if (m_pEngine)
            m_pEngine->setPitch(m_fPitch);
        emit changed();
    }
    void ReadAloudController::setVolume(double fValue)
    {
        m_fVolume = std::isfinite(fValue) ? qBound(0.0, fValue, 1.0) : 1.0;
        if (m_pEngine)
            m_pEngine->setVolume(m_fVolume);
        emit changed();
    }
    void ReadAloudController::setAllowNetworkVoice(bool bValue)
    {
        m_bAllowNetworkVoice = bValue;
        if (!bValue && m_pEngine && m_pEngine->requiresNetwork() &&
            (m_eState == State::Playing || m_eState == State::WaitingChapter))
            pause("联网音色已暂停");
        emit changed();
    }
    int ReadAloudController::startOffset() const
    {
        return m_vSegments.isEmpty() ? 0 : m_vSegments[m_nIndex].m_nStart;
    }
    int ReadAloudController::endOffset() const
    {
        return m_vSegments.isEmpty() ? 0 : m_vSegments[m_nIndex].m_nEnd;
    }
    QString ReadAloudController::currentText() const
    {
        return m_vSegments.isEmpty() ? QString{} : m_vSegments[m_nIndex].m_strText;
    }
    void ReadAloudController::clearChapterRequest()
    {
        m_bChapterPending = false;
        m_bResumeAfterChapter = false;
    }
    int ReadAloudController::indexForOffset(int nOffset) const
    {
        for (int nIndex = 0; nIndex < m_vSegments.size(); ++nIndex)
            if (nOffset < m_vSegments[nIndex].m_nEnd)
                return nIndex;
        return qMax(0, m_vSegments.size() - 1);
    }
    bool ReadAloudController::canResume(int nBook) const
    {
        return m_eState == State::WaitingChapter && m_bChapterPending && m_bResumeAfterChapter &&
               m_nBook == nBook;
    }
    bool ReadAloudController::ensureVoiceReady()
    {
        if (!m_pEngine || !m_pEngine->available())
        {
            setState(State::Error, m_pEngine && !m_pEngine->errorString().isEmpty()
                                       ? m_pEngine->errorString()
                                       : "系统语音尚未就绪，请安装中文语音或稍后重试。");
            return false;
        }
        if (m_pEngine->requiresNetwork() && !m_bAllowNetworkVoice)
        {
            setState(State::Paused, "所选系统音色需要联网，请先允许联网合成或选择离线音色。");
            return false;
        }
        return true;
    }
} // namespace Wenku8::Reader
