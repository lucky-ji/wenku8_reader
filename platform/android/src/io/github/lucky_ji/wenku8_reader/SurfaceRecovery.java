// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import java.util.ArrayList;

/** One bounded Qt surface release/resume cycle; all calls run on Android's UI thread. */
final class SurfaceRecovery
{
    interface FrameScheduler
    {
        void post(Runnable fnAction);
    }
    interface TimeoutScheduler
    {
        void post(Runnable fnAction, long nDelay);
    }
    private final Runnable m_fnRelease;
    private final Runnable m_fnResume;
    private final FrameScheduler m_fnFrame;
    private final TimeoutScheduler m_fnTimeout;
    private final ArrayList<Runnable> m_vCompletions = new ArrayList<>();
    private boolean m_bForeground = false, m_bPending = false, m_bReleased = false;
    private boolean m_bStarted = false, m_bFrameQueued = false, m_bClosed = false;
    private long m_nGeneration = 0;

    SurfaceRecovery(Runnable fnRelease, Runnable fnResume, FrameScheduler fnFrame,
                    TimeoutScheduler fnTimeout)
    {
        m_fnRelease = fnRelease;
        m_fnResume = fnResume;
        m_fnFrame = fnFrame;
        m_fnTimeout = fnTimeout;
    }
    void request(Runnable fnComplete)
    {
        if (m_bClosed)
        {
            return;
        }
        m_vCompletions.add(fnComplete);
        if (!m_bPending)
        {
            m_bPending = true;
            ++m_nGeneration;
        }
        advance();
    }
    void setForeground(boolean bForeground)
    {
        m_bForeground = bForeground;
        advance();
    }
    void released()
    {
        if (m_bClosed || !m_bPending || !m_bStarted)
        {
            return;
        }
        m_bReleased = true;
        advance();
    }
    private void advance()
    {
        if (m_bClosed || !m_bForeground || !m_bPending)
        {
            return;
        }
        if (!m_bStarted)
        {
            m_bStarted = true;
            final long nGeneration = m_nGeneration;
            m_fnRelease.run();
            // A missing lifecycle acknowledgement must not leave Qt suspended.
            m_fnTimeout.post(() ->
            {
                if (nGeneration == m_nGeneration)
                {
                    released();
                }
            }, 500);
        }
        if (!m_bReleased || m_bFrameQueued)
        {
            return;
        }
        m_bFrameQueued = true;
        m_fnFrame.post(() ->
        {
            m_bFrameQueued = false;
            if (m_bClosed || !m_bForeground || !m_bPending)
            {
                return;
            }
            m_bPending = false;
            m_bStarted = false;
            m_bReleased = false;
            ArrayList<Runnable> vCompletions = new ArrayList<>(m_vCompletions);
            m_vCompletions.clear();
            m_fnResume.run();
            for (Runnable fnComplete : vCompletions)
            {
                fnComplete.run();
            }
        });
    }
    void close()
    {
        m_bClosed = true;
        m_vCompletions.clear();
    }
}
