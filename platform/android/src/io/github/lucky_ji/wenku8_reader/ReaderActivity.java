// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;
import android.util.SparseBooleanArray;
import android.view.KeyEvent;
import org.qtproject.qt5.android.bindings.QtActivity;
import org.qtproject.qt5.android.QtNative;

public class ReaderActivity extends QtActivity
{
    private static volatile boolean m_bVolumePagingEnabled = false;
    private final SparseBooleanArray m_oCaptured = new SparseBooleanArray();
    private boolean m_bResumed = false;
    private boolean m_bQtRefreshPending = false;
    private boolean m_bQtFrameQueued = false;

    public void restoreQtRendering()
    {
        m_bQtRefreshPending = true;
        scheduleQtRefresh();
    }
    private void scheduleQtRefresh()
    {
        if (!m_bQtRefreshPending || m_bQtFrameQueued || !m_bResumed || !hasWindowFocus() ||
            BrowserLogin.showing() || isFinishing() || isDestroyed())
        {
            return;
        }
        m_bQtFrameQueued = true;
        getWindow().getDecorView().postOnAnimation(() ->
        {
            m_bQtFrameQueued = false;
            if (!m_bQtRefreshPending || !m_bResumed || !hasWindowFocus() ||
                BrowserLogin.showing() || isFinishing() || isDestroyed())
            {
                return;
            }
            m_bQtRefreshPending = false;
            // Qt 5's onResume uses this hook to expose all windows and mark the
            // Android raster surface dirty. View.invalidate() cannot do that.
            // Wait for an Android frame after removing the WebView overlay.
            QtNative.updateWindow();
        });
    }
    @Override public void onResume()
    {
        super.onResume();
        m_bResumed = true;
        scheduleQtRefresh();
    }
    @Override public void onWindowFocusChanged(boolean bHasFocus)
    {
        super.onWindowFocusChanged(bHasFocus);
        if (bHasFocus)
        {
            scheduleQtRefresh();
        }
    }
    public static void setVolumePagingEnabled(boolean bEnabled)
    {
        m_bVolumePagingEnabled = bEnabled;
    }
    private static native void nativeVolumePage(int nDirection);

    @Override public boolean dispatchKeyEvent(KeyEvent oEvent)
    {
        int nKey = oEvent.getKeyCode();
        if (nKey == KeyEvent.KEYCODE_BACK && BrowserLogin.showing())
        {
            if (oEvent.getAction() == KeyEvent.ACTION_UP)
                BrowserLogin.back();
            return true;
        }
        if (nKey == KeyEvent.KEYCODE_VOLUME_UP || nKey == KeyEvent.KEYCODE_VOLUME_DOWN)
        {
            if (oEvent.getAction() == KeyEvent.ACTION_UP && m_oCaptured.get(nKey))
            {
                m_oCaptured.delete(nKey);
                return true;
            }
            if (oEvent.getAction() == KeyEvent.ACTION_DOWN &&
                ((m_bVolumePagingEnabled && !BrowserLogin.showing()) || m_oCaptured.get(nKey)))
            {
                m_oCaptured.put(nKey, true);
                if (m_bVolumePagingEnabled && oEvent.getRepeatCount() == 0)
                    nativeVolumePage(nKey == KeyEvent.KEYCODE_VOLUME_UP ? -1 : 1);
                return true;
            }
        }
        return super.dispatchKeyEvent(oEvent);
    }
    @Override public void onBackPressed()
    {
        if (!BrowserLogin.back())
            super.onBackPressed();
    }
    @Override public void onDestroy()
    {
        m_bResumed = false;
        m_bQtRefreshPending = false;
        BrowserLogin.destroy();
        super.onDestroy();
    }
    @Override public void onPause()
    {
        m_bResumed = false;
        BrowserLogin.persist();
        m_bVolumePagingEnabled = false;
        m_oCaptured.clear();
        super.onPause();
    }
}
