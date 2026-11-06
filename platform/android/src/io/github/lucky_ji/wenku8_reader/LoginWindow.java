// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.app.Activity;
import android.app.Dialog;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

/** Keeps WebView composition in a separate window from Qt's raster SurfaceView. */
final class LoginWindow
{
    private final Dialog m_oDialog;
    private boolean m_bVisible = false;
    private boolean m_bClosed = false;

    LoginWindow(Activity oOwner, View oContent, Runnable fnCancel)
    {
        m_oDialog = new Dialog(oOwner, android.R.style.Theme_Material_Light_NoActionBar);
        m_oDialog.setContentView(oContent);
        m_oDialog.setCanceledOnTouchOutside(false);
        m_oDialog.setOnCancelListener(oDialog ->
        {
            fnCancel.run();
            m_bVisible = false;
        });
        m_oDialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED);
        m_oDialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }
    void show()
    {
        if (m_bClosed)
        {
            return;
        }
        m_oDialog.show();
        m_oDialog.getWindow().setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                                       ViewGroup.LayoutParams.MATCH_PARENT);
        m_bVisible = true;
    }
    void hide()
    {
        m_bVisible = false;
        // Keep the same WebView attached to its own window. Its authenticated
        // JavaScript transport must remain usable after the login UI closes.
        m_oDialog.hide();
    }
    boolean visible()
    {
        return m_bVisible;
    }
    void close()
    {
        m_bClosed = true;
        m_bVisible = false;
        m_oDialog.dismiss();
    }
}
