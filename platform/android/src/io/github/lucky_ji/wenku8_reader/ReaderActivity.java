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
    private static volatile boolean m_bReadingActive = false;
    private static volatile boolean m_bReadingKeepAwake = false;
    private final SparseBooleanArray m_oCaptured = new SparseBooleanArray();
    private final Handler m_oMainHandler = new Handler(Looper.getMainLooper());
    private final ReadingWindow m_oReadingWindow = new ReadingWindow(this);
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
        m_oReadingWindow.attach();
        m_oReadingWindow.setPreferences(m_bReadingActive, m_bReadingKeepAwake);
        m_oReadingWindow.setFocused(hasWindowFocus());
        m_oReadingWindow.setForeground(true);
        m_oSurfaceRecovery.setForeground(true);
    }
    @Override public void onWindowFocusChanged(boolean bHasFocus)
    {
        super.onWindowFocusChanged(bHasFocus);
        m_oReadingWindow.setFocused(bHasFocus);
    }
    public static void setReadingPreferences(boolean bActive, boolean bKeepAwake)
    {
        m_bReadingActive = bActive;
        m_bReadingKeepAwake = bKeepAwake;
        final android.app.Activity oActivity = QtNative.activity();
        if (oActivity instanceof ReaderActivity)
        {
            final ReaderActivity oReader = (ReaderActivity) oActivity;
            oReader.runOnUiThread(() -> oReader.m_oReadingWindow.setPreferences(
                m_bReadingActive, m_bReadingKeepAwake));
        }
    }
    public static int[] getReadingSafeInsets()
    {
        final android.app.Activity oActivity = QtNative.activity();
        if (oActivity instanceof ReaderActivity)
        {
            return ((ReaderActivity) oActivity).m_oReadingWindow.safeInsets();
        }
        return new int[] {0, 0, 0, 0};
    }
    static boolean notifyReadingInsetsChanged()
    {
        try
        {
            nativeReadingInsetsChanged();
            return true;
        }
        catch (UnsatisfiedLinkError oIgnored)
        {
            // Layout can precede Qt's native library initialization. Retry on the next layout/resume.
            return false;
        }
    }
    public static void setVolumePagingEnabled(boolean bEnabled)
    {
        m_bVolumePagingEnabled = bEnabled;
    }
    private static native void nativeVolumePage(int nDirection);
    private static native void nativeReadingInsetsChanged();

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
        m_oReadingWindow.close();
        m_oSurfaceRecovery.close();
        BrowserLogin.destroy();
        super.onDestroy();
    }
    @Override public void onPause()
    {
        m_oReadingWindow.setForeground(false);
        m_oSurfaceRecovery.setForeground(false);
        BrowserLogin.persist();
        m_bVolumePagingEnabled = false;
        m_oCaptured.clear();
        super.onPause();
    }
}
