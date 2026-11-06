// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;
import android.util.SparseBooleanArray;
import android.view.KeyEvent;
import org.qtproject.qt5.android.bindings.QtActivity;
import org.qtproject.qt5.android.QtNative;
import org.qtproject.qt5.android.QtActivityDelegate;
import android.os.Handler;
import android.os.Looper;

public class ReaderActivity extends QtActivity
{
    private static volatile boolean m_bVolumePagingEnabled = false;
    private final SparseBooleanArray m_oCaptured = new SparseBooleanArray();
    private final Handler m_oMainHandler = new Handler(Looper.getMainLooper());
    private final SurfaceRecovery m_oSurfaceRecovery = new SurfaceRecovery(
        () -> QtNative.setApplicationState(QtActivityDelegate.ApplicationHidden),
        () ->
        {
            QtNative.setApplicationState(QtActivityDelegate.ApplicationActive);
            QtNative.updateWindow();
        },
        fnAction -> getWindow().getDecorView().postOnAnimation(fnAction),
        (fnAction, nDelay) -> m_oMainHandler.postDelayed(fnAction, nDelay));

    public void rebuildQtSurface(Runnable fnComplete)
    {
        m_oSurfaceRecovery.request(fnComplete);
    }
    public void qtSurfaceReleased()
    {
        m_oSurfaceRecovery.released();
    }
    @Override public void onResume()
    {
        super.onResume();
        m_oSurfaceRecovery.setForeground(true);
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
        m_oSurfaceRecovery.close();
        BrowserLogin.destroy();
        super.onDestroy();
    }
    @Override public void onPause()
    {
        m_oSurfaceRecovery.setForeground(false);
        BrowserLogin.persist();
        m_bVolumePagingEnabled = false;
        m_oCaptured.clear();
        super.onPause();
    }
}
