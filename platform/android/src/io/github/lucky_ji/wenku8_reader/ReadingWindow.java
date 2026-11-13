// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import java.lang.reflect.Method;
import java.util.Arrays;

/** Reading-only window flags and read-only safe-area observation. All mutation runs on the UI thread. */
final class ReadingWindow
{
    private final Activity m_oActivity;
    private boolean m_bActive = false;
    private boolean m_bKeepAwake = false;
    private boolean m_bForeground = false;
    private boolean m_bFocused = false;
    private boolean m_bAppliedKeepAwake = false;
    private boolean m_bOriginalKeepAwake = false;
    private boolean m_bInsetsNotified = false;
    private boolean m_bClosed = false;
    // Physical pixels relative to the already positioned Activity content, never density-independent units.
    private volatile int[] m_vSafeInsets = new int[] {0, 0, 0, 0};
    private View m_oDecor;
    private final ViewTreeObserver.OnGlobalLayoutListener m_fnLayout = () -> refresh();

    ReadingWindow(Activity oActivity)
    {
        m_oActivity = oActivity;
    }

    void attach()
    {
        if (m_oDecor != null || m_bClosed)
        {
            return;
        }
        m_oDecor = m_oActivity.getWindow().getDecorView();
        m_oDecor.getViewTreeObserver().addOnGlobalLayoutListener(m_fnLayout);
        refresh();
    }

    void setPreferences(boolean bActive, boolean bKeepAwake)
    {
        m_bActive = bActive;
        m_bKeepAwake = bKeepAwake;
        refresh();
    }

    void setForeground(boolean bForeground)
    {
        m_bForeground = bForeground;
        refresh();
    }

    void setFocused(boolean bFocused)
    {
        m_bFocused = bFocused;
        refresh();
    }

    int[] safeInsets()
    {
        return m_vSafeInsets.clone();
    }

    void close()
    {
        m_bClosed = true;
        m_bActive = false;
        m_bForeground = false;
        applyKeepAwake();
        if (m_oDecor != null && m_oDecor.getViewTreeObserver().isAlive())
        {
            m_oDecor.getViewTreeObserver().removeOnGlobalLayoutListener(m_fnLayout);
        }
        m_oDecor = null;
    }

    private void applyKeepAwake()
    {
        final boolean bWanted = !m_bClosed && m_bActive && m_bKeepAwake && m_bForeground &&
                                  m_bFocused && !BrowserLogin.showing();
        final Window oWindow = m_oActivity.getWindow();
        if (bWanted && !m_bAppliedKeepAwake)
        {
            m_bOriginalKeepAwake =
                (oWindow.getAttributes().flags & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) != 0;
            if (!m_bOriginalKeepAwake)
            {
                oWindow.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
            m_bAppliedKeepAwake = true;
        }
        else if (!bWanted && m_bAppliedKeepAwake)
        {
            if (!m_bOriginalKeepAwake)
            {
                oWindow.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
            m_bAppliedKeepAwake = false;
        }
    }

    private void refresh()
    {
        if (m_bClosed)
        {
            return;
        }
        applyKeepAwake();
        final int[] vInsets = readSafeInsets();
        if (!Arrays.equals(vInsets, m_vSafeInsets))
        {
            m_vSafeInsets = vInsets;
            m_bInsetsNotified = false;
        }
        if (!m_bInsetsNotified)
        {
            m_bInsetsNotified = ReaderActivity.notifyReadingInsetsChanged();
        }
    }

    private static void mergeInsets(int[] vTarget, Object oInsets) throws ReflectiveOperationException
    {
        final String[] vFields = {"left", "top", "right", "bottom"};
        for (int nIndex = 0; nIndex < vFields.length; ++nIndex)
        {
            vTarget[nIndex] = Math.max(vTarget[nIndex],
                oInsets.getClass().getField(vFields[nIndex]).getInt(oInsets));
        }
    }

    private int[] readSafeInsets()
    {
        final int[] vInsets = {0, 0, 0, 0};
        if (Build.VERSION.SDK_INT < 23 || m_oDecor == null || m_oDecor.getWidth() <= 0 ||
            m_oDecor.getHeight() <= 0)
        {
            return vInsets;
        }
        final WindowInsets oInsets = m_oDecor.getRootWindowInsets();
        if (oInsets == null)
        {
            return vInsets;
        }
        if (Build.VERSION.SDK_INT >= 30)
        {
            // Reflection keeps the Qt 5 / compileSdk 28 toolchain while reading typed
            // system-bar, cutout and mandatory gesture insets on newer Android versions.
            // The IME type is deliberately excluded: adjustResize already handles it.
            try
            {
                final Class<?> oTypes = Class.forName("android.view.WindowInsets$Type");
                int nTypes = 0;
                for (String strType : new String[]
                    {"systemBars", "displayCutout", "systemGestures", "mandatorySystemGestures"})
                {
                    nTypes |= (Integer) oTypes.getMethod(strType).invoke(null);
                }
                final Method oGetInsets = WindowInsets.class.getMethod("getInsets", int.class);
                mergeInsets(vInsets, oGetInsets.invoke(oInsets, nTypes));
            }
            catch (ReflectiveOperationException | RuntimeException oIgnored)
            {
                readLegacyInsets(oInsets, vInsets);
            }
        }
        else
        {
            readLegacyInsets(oInsets, vInsets);
        }

        // Decor coordinates include system UI; Qt's Activity content may already be
        // fitted inside those bars. Return only the remaining overlap, not the bars twice.
        final View oContent = m_oDecor.findViewById(android.R.id.content);
        if (oContent == null || oContent.getWidth() <= 0 || oContent.getHeight() <= 0)
        {
            return new int[] {0, 0, 0, 0};
        }
        final int[] vDecorPosition = new int[2];
        final int[] vContentPosition = new int[2];
        m_oDecor.getLocationOnScreen(vDecorPosition);
        oContent.getLocationOnScreen(vContentPosition);
        final int nLeft = vContentPosition[0] - vDecorPosition[0];
        final int nTop = vContentPosition[1] - vDecorPosition[1];
        final int nRight = m_oDecor.getWidth() - nLeft - oContent.getWidth();
        final int nBottom = m_oDecor.getHeight() - nTop - oContent.getHeight();
        vInsets[0] = Math.max(0, vInsets[0] - Math.max(0, nLeft));
        vInsets[1] = Math.max(0, vInsets[1] - Math.max(0, nTop));
        vInsets[2] = Math.max(0, vInsets[2] - Math.max(0, nRight));
        vInsets[3] = Math.max(0, vInsets[3] - Math.max(0, nBottom));
        return vInsets;
    }

    private static void readLegacyInsets(WindowInsets oInsets, int[] vTarget)
    {
        // Stable bars exclude the soft keyboard from the legacy combined bottom inset.
        vTarget[0] = Math.min(oInsets.getSystemWindowInsetLeft(), oInsets.getStableInsetLeft());
        vTarget[1] = Math.min(oInsets.getSystemWindowInsetTop(), oInsets.getStableInsetTop());
        vTarget[2] = Math.min(oInsets.getSystemWindowInsetRight(), oInsets.getStableInsetRight());
        vTarget[3] = Math.min(oInsets.getSystemWindowInsetBottom(), oInsets.getStableInsetBottom());
        if (Build.VERSION.SDK_INT >= 28 && oInsets.getDisplayCutout() != null)
        {
            vTarget[0] = Math.max(vTarget[0], oInsets.getDisplayCutout().getSafeInsetLeft());
            vTarget[1] = Math.max(vTarget[1], oInsets.getDisplayCutout().getSafeInsetTop());
            vTarget[2] = Math.max(vTarget[2], oInsets.getDisplayCutout().getSafeInsetRight());
            vTarget[3] = Math.max(vTarget[3], oInsets.getDisplayCutout().getSafeInsetBottom());
        }
        if (Build.VERSION.SDK_INT >= 29)
        {
            try
            {
                for (String strMethod : new String[]
                    {"getSystemGestureInsets", "getMandatorySystemGestureInsets"})
                {
                    mergeInsets(vTarget, WindowInsets.class.getMethod(strMethod).invoke(oInsets));
                }
            }
            catch (ReflectiveOperationException | RuntimeException oIgnored)
            {
            }
        }
    }
}
