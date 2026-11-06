// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.app.Activity;
import android.graphics.Color;
import android.content.res.ColorStateList;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.*;
import android.widget.*;
import org.json.*;
import java.io.ByteArrayInputStream;

/**
 * Browser owns cookies. No JavascriptInterface, file chooser, password bridge or permissive SSL
 * handler.
 */
public final class BrowserLogin
{
    private static BrowserLogin m_oInstance;
    private final Activity m_oActivity;
    private final long m_nToken;
    private final String m_strOrigin;
    private final NavigationPolicy m_oNavigation;
    private final String m_strNavigationScript;
    private final FrameLayout m_oOverlay;
    private final WebView m_oWeb;
    private final TextView m_oHint;
    private final FrameLayout m_oPageLoading;
    private final ProgressBar m_oPageProgress;
    private long m_nNavigationGeneration = 0;
    private int dp(int nValue)
    {
        return Math.round(nValue * m_oActivity.getResources().getDisplayMetrics().density);
    }
    private void loading(boolean bBusy)
    {
        m_oWeb.setEnabled(!bBusy);
        m_oPageLoading.setVisibility(bBusy ? View.VISIBLE : View.GONE);
        if (bBusy)
        {
            m_oPageProgress.setProgress(0);
            m_oPageLoading.bringToFront();
        }
    }
    private final Handler m_oHandler = new Handler(Looper.getMainLooper());
    private long m_nJob = 0;
    private boolean m_bClosed = false, m_bLoaded = false;
    private String m_strPageError = "";
    private static native void nativeEvent(long nToken, int nEvent, String strData);
    private static native void nativeReply(long nToken, long nRequest, String strJson);
    private static void ui(Runnable fnAction)
    {
        new Handler(Looper.getMainLooper()).post(fnAction);
    }
    private boolean same(String strUrl)
    {
        return m_oNavigation.same(strUrl);
    }
    private boolean resource(String strUrl)
    {
        Uri oUri = Uri.parse(strUrl);
        String strHost = oUri.getHost();
        return same(strUrl) || "about:blank".equals(strUrl) || "data".equals(oUri.getScheme()) ||
            ("https".equals(oUri.getScheme()) && oUri.getUserInfo() == null &&
             (oUri.getPort() == -1 || oUri.getPort() == 443) &&
             ("challenges.cloudflare.com".equals(strHost) || "img.wenku8.com".equals(strHost)));
    }
    private void error(String strMessage)
    {
        loading(false);
        m_strPageError = strMessage;
        m_oHint.setText("暂时无法打开登录页面，请检查网络后重试。");
        m_oHint.setVisibility(View.VISIBLE);
    }
    private boolean navigate(WebView oView, String strUrl, boolean bMain, String strMethod)
    {
        if (!bMain)
            return !resource(strUrl);
        if (same(strUrl))
            return false;
        String strSecure = m_oNavigation.canonical(strUrl);
        // Only a legacy GET navigation is replaced. A login POST must retain
        // its body and method: the DOM guard upgrades its form action instead.
        if (strSecure != null && "GET".equalsIgnoreCase(strMethod))
        {
            oView.loadUrl(strSecure);
            return true;
        }
        error("此链接无法在登录页中打开，请使用当前文库的 HTTPS 站内链接。");
        return true;
    }
    private BrowserLogin(Activity oOwner, String strSite, String strGuard, long nId)
    {
        m_oActivity = oOwner;
        m_strOrigin = strSite;
        m_nToken = nId;
        m_oNavigation = new NavigationPolicy(strSite);
        m_strNavigationScript = strGuard;
        m_oOverlay = new FrameLayout(oOwner);
        m_oOverlay.setBackgroundColor(Color.WHITE);
        m_oOverlay.setVisibility(View.GONE);
        LinearLayout oColumn = new LinearLayout(oOwner);
        oColumn.setOrientation(LinearLayout.VERTICAL);
        m_oOverlay.addView(oColumn, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout oBar = new LinearLayout(oOwner);
        oBar.setGravity(Gravity.CENTER_VERTICAL);
        oBar.setPadding(dp(12), dp(8), dp(16), dp(8));
        oBar.setBackgroundColor(Color.rgb(248, 250, 252));
        oColumn.addView(oBar);
        TextView oCancel = new TextView(oOwner);
        oCancel.setText("‹  返回");
        oCancel.setTextSize(16);
        oCancel.setTextColor(Color.rgb(49, 91, 70));
        oCancel.setGravity(Gravity.CENTER);
        oCancel.setPadding(dp(14), 0, dp(14), 0);
        oCancel.setMinHeight(dp(44));
        oCancel.setContentDescription("返回客户端");
        GradientDrawable oBackSurface = new GradientDrawable();
        oBackSurface.setColor(Color.rgb(231, 239, 249));
        oBackSurface.setCornerRadius(dp(14));
        oCancel.setBackground(new RippleDrawable(ColorStateList.valueOf(Color.rgb(206, 215, 227)),
                                                 oBackSurface, null));
        oCancel.setOnClickListener(oV -> cancel());
        oBar.addView(oCancel);
        TextView oTitle = new TextView(oOwner);
        oTitle.setText("登录");
        oTitle.setTextColor(Color.rgb(35, 48, 68));
        oTitle.setTextSize(18);
        oBar.addView(oTitle, new LinearLayout.LayoutParams(0, -2, 1));
        m_oHint = new TextView(oOwner);
        m_oHint.setPadding(16, 4, 16, 8);
        m_oHint.setTextColor(Color.DKGRAY);
        m_oHint.setVisibility(View.GONE);
        oColumn.addView(m_oHint);
        FrameLayout oPage = new FrameLayout(oOwner);
        oColumn.addView(oPage, new LinearLayout.LayoutParams(-1, 0, 1));
        m_oWeb = new WebView(oOwner);
        oPage.addView(m_oWeb, new FrameLayout.LayoutParams(-1, -1));
        m_oPageLoading = new FrameLayout(oOwner);
        m_oPageLoading.setBackgroundColor(Color.rgb(248, 250, 252));
        m_oPageLoading.setClickable(true);
        m_oPageLoading.setFocusable(true);
        m_oPageLoading.setContentDescription("正在打开登录页面，请稍候");
        LinearLayout oLoadingContent = new LinearLayout(oOwner);
        oLoadingContent.setOrientation(LinearLayout.VERTICAL);
        oLoadingContent.setGravity(Gravity.CENTER);
        ProgressBar oSpinner = new ProgressBar(oOwner);
        oSpinner.setIndeterminateTintList(ColorStateList.valueOf(Color.rgb(49, 91, 70)));
        oLoadingContent.addView(oSpinner, new LinearLayout.LayoutParams(dp(40), dp(40)));
        TextView oLoadingTitle = new TextView(oOwner);
        oLoadingTitle.setText("正在打开登录页面…");
        oLoadingTitle.setTextColor(Color.rgb(35, 48, 68));
        oLoadingTitle.setTextSize(16);
        oLoadingTitle.setPadding(0, dp(24), 0, dp(18));
        oLoadingContent.addView(oLoadingTitle);
        m_oPageProgress = new ProgressBar(oOwner, null, android.R.attr.progressBarStyleHorizontal);
        m_oPageProgress.setMax(100);
        m_oPageProgress.setProgressTintList(ColorStateList.valueOf(Color.rgb(49, 91, 70)));
        m_oPageProgress.setProgressBackgroundTintList(
            ColorStateList.valueOf(Color.rgb(223, 230, 239)));
        oLoadingContent.addView(m_oPageProgress, new LinearLayout.LayoutParams(dp(180), dp(5)));
        m_oPageLoading.addView(oLoadingContent,
                               new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER));
        oPage.addView(m_oPageLoading, new FrameLayout.LayoutParams(-1, -1));
        m_oPageLoading.setVisibility(View.GONE);
        m_oWeb.setOnLongClickListener(oV ->
        {
            int nType = m_oWeb.getHitTestResult().getType();
            return nType == WebView.HitTestResult.SRC_ANCHOR_TYPE ||
                nType == WebView.HitTestResult.SRC_IMAGE_ANCHOR_TYPE;
        });
        WebSettings oSettings = m_oWeb.getSettings();
        oSettings.setJavaScriptEnabled(true);
        oSettings.setDomStorageEnabled(false);
        oSettings.setSavePassword(false);
        oSettings.setSaveFormData(false);
        if (android.os.Build.VERSION.SDK_INT >= 26)
            m_oWeb.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        oSettings.setAllowFileAccess(false);
        oSettings.setAllowContentAccess(false);
        oSettings.setAllowFileAccessFromFileURLs(false);
        oSettings.setAllowUniversalAccessFromFileURLs(false);
        oSettings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        oSettings.setJavaScriptCanOpenWindowsAutomatically(false);
        oSettings.setSupportMultipleWindows(false);
        oSettings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        if (android.os.Build.VERSION.SDK_INT >= 26)
            oSettings.setSafeBrowsingEnabled(true);
        CookieManager.getInstance().setAcceptCookie(true);
        CookieManager.getInstance().setAcceptThirdPartyCookies(m_oWeb, false);
        m_oWeb.setWebChromeClient(new WebChromeClient()
        {
            @Override public void onProgressChanged(WebView oView, int nProgress)
            {
                m_oPageProgress.setProgress(nProgress);
            }
            @Override public boolean onConsoleMessage(ConsoleMessage oM)
            {
                return true;
            }
            @Override public void onPermissionRequest(PermissionRequest oRequest)
            {
                oRequest.deny();
            }
            @Override
            public boolean onShowFileChooser(WebView oV, android.webkit.ValueCallback<Uri[]> oCb,
                                             FileChooserParams oP)
            {
                oCb.onReceiveValue(null);
                return true;
            }
            @Override public boolean onJsAlert(WebView oV, String strU, String strM, JsResult oR)
            {
                oR.confirm();
                return true;
            }
            @Override public boolean onJsConfirm(WebView oV, String strU, String strM, JsResult oR)
            {
                new android.app.AlertDialog.Builder(m_oActivity)
                    .setMessage(strM)
                    .setPositiveButton("确定", (oD, nW) -> oR.confirm())
                    .setNegativeButton("取消", (oD, nW) -> oR.cancel())
                    .setOnCancelListener(oD -> oR.cancel())
                    .show();
                return true;
            }
            @Override
            public boolean onJsPrompt(WebView oV, String strU, String strM, String strD,
                                      JsPromptResult oR)
            {
                oR.cancel();
                return true;
            }
        });
        m_oWeb.setWebViewClient(new WebViewClient()
        {
            @Override public boolean shouldOverrideUrlLoading(WebView oView, WebResourceRequest oR)
            {
                return navigate(oView, oR.getUrl().toString(), oR.isForMainFrame(), oR.getMethod());
            }
            @Override public boolean shouldOverrideUrlLoading(WebView oView, String strUrl)
            {
                return navigate(oView, strUrl, true, "GET");
            }
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView oView, WebResourceRequest oR)
            {
                if (!resource(oR.getUrl().toString()))
                    return new WebResourceResponse("text/plain", "utf-8", 403, "Blocked",
                                                   java.util.Collections.emptyMap(),
                                                   new ByteArrayInputStream(new byte[0]));
                return null;
            }
            @Override
            public void onReceivedSslError(WebView oView, SslErrorHandler oHandler,
                                           SslError oFailure)
            {
                oHandler.cancel();
                error("网页证书校验失败，请检查设备时间与网络环境。");
            }
            @Override
            public void onPageStarted(WebView oView, String strUrl, android.graphics.Bitmap oIcon)
            {
                m_bLoaded = false;
                m_strPageError = "";
                m_oHint.setVisibility(View.GONE);
                loading(true);
                nativeEvent(m_nToken, 5, "");
                abort("navigation");
                final long nGeneration = ++m_nNavigationGeneration;
                m_oHandler.postDelayed(() ->
                {
                    if (!m_bClosed && nGeneration == m_nNavigationGeneration &&
                        m_oPageLoading.getVisibility() == View.VISIBLE)
                        error("网页加载超时，请检查网络后重试。");
                }, 30000);
            }
            @Override public void onPageCommitVisible(WebView oView, String strUrl)
            {
                if (same(strUrl))
                    oView.evaluateJavascript(m_strNavigationScript, null);
            }
            @Override
            public void onReceivedError(WebView oView, WebResourceRequest oRequest,
                                        WebResourceError oError)
            {
                if (oRequest.isForMainFrame())
                    error("网页加载失败（错误码 " + oError.getErrorCode() +
                          "），请检查网络并重试。");
            }
            @Override
            public void onReceivedHttpError(WebView oView, WebResourceRequest oRequest,
                                            WebResourceResponse oResponse)
            {
                if (oRequest.isForMainFrame())
                    error("网站返回 HTTP " + oResponse.getStatusCode() +
                          "，请完成验证或稍后重试。");
            }
            @Override public void onPageFinished(WebView oView, String strUrl)
            {
                if (!same(strUrl) || !strUrl.equals(oView.getUrl()))
                    return;
                oView.evaluateJavascript(m_strNavigationScript, strResult ->
                {
                    if (m_bClosed || !strUrl.equals(m_oWeb.getUrl()))
                        return;
                    loading(false);
                    m_bLoaded = "true".equals(strResult);
                    if (m_bLoaded)
                    {
                        CookieManager.getInstance().flush();
                        nativeEvent(m_nToken, 1, "");
                    }
                    else if (m_strPageError.isEmpty())
                        error("网页登录页面未准备好，请重试或更新系统 WebView。");
                });
            }
            @Override
            public boolean onRenderProcessGone(WebView oView, RenderProcessGoneDetail oDetail)
            {
                m_bLoaded = false;
                abort("network");
                hide();
                nativeEvent(m_nToken, 4, "");
                return true;
            }
        });
        m_oWeb.setDownloadListener((strU, strA, strC, strM, nL) ->
        {
        });
        m_oActivity.addContentView(m_oOverlay, new ViewGroup.LayoutParams(-1, -1));
        nativeEvent(m_nToken, 0, oSettings.getUserAgentString());
    }
    public static void create(Activity oOwner, String strOrigin, String strGuard, long nToken)
    {
        ui(() ->
        {
            try
            {
                destroy();
                m_oInstance = new BrowserLogin(oOwner, strOrigin, strGuard, nToken);
            }
            catch (RuntimeException oE)
            {
                nativeEvent(nToken, 4, "");
            }
        });
    }
    public static void command(long nToken, int nAction, String strValue)
    {
        ui(() ->
        {
            BrowserLogin oB = m_oInstance;
            if (oB == null || oB.m_nToken != nToken || oB.m_bClosed)
                return;
            if (nAction == 0)
            {
                oB.m_oOverlay.setVisibility(View.VISIBLE);
                oB.m_oOverlay.bringToFront();
                oB.m_oWeb.requestFocus();
            }
            else if (nAction == 1)
                oB.open();
            else if (nAction == 2)
            {
                oB.hide();
                CookieManager.getInstance().flush();
            }
            else if (nAction == 3)
            {
            } // Session checks keep the login surface unchanged.
            else if (nAction == 4)
            {
                oB.abort("cancel");
                oB.m_bLoaded = false;
                oB.m_oWeb.stopLoading();
                CookieManager.getInstance().removeAllCookies(oV ->
                {
                    CookieManager.getInstance().flush();
                    oB.m_oWeb.clearCache(true);
                    oB.m_oWeb.clearHistory();
                    oB.open();
                });
            }
            else if (nAction == 5)
            {
                persist();
                oB.abort("cancel");
                oB.m_bClosed = true;
                oB.m_oWeb.stopLoading();
                oB.m_oWeb.destroy();
                ((ViewGroup)oB.m_oOverlay.getParent()).removeView(oB.m_oOverlay);
                m_oInstance = null;
            }
            else if (nAction == 6)
            {
                oB.abort("cancel");
            }
        });
    }
    private void open()
    {
        if (!m_bClosed)
        {
            loading(true);
            m_oWeb.loadUrl(m_strOrigin + "/login.php");
        }
    }
    private void hide()
    {
        android.view.inputmethod.InputMethodManager oKeyboard =
            (android.view.inputmethod.InputMethodManager)m_oActivity.getSystemService(
                android.content.Context.INPUT_METHOD_SERVICE);
        oKeyboard.hideSoftInputFromWindow(m_oWeb.getWindowToken(), 0);
        m_oWeb.clearFocus();
        m_oOverlay.setVisibility(View.GONE);
    }
    private void cancel()
    {
        abort("cancel");
        hide();
        nativeEvent(m_nToken, 2, "");
    }
    public static boolean showing()
    {
        BrowserLogin oB = m_oInstance;
        return oB != null && oB.m_oOverlay.getVisibility() == View.VISIBLE;
    }
    public static void persist()
    {
        try
        {
            CookieManager.getInstance().flush();
        }
        catch (RuntimeException oIgnored)
        {
        }
    }
    public static boolean back()
    {
        BrowserLogin oB = m_oInstance;
        if (oB != null && oB.m_oOverlay.getVisibility() == View.VISIBLE)
        {
            oB.cancel();
            return true;
        }
        return false;
    }
    public static void destroy()
    {
        BrowserLogin oB = m_oInstance;
        if (oB != null)
        {
            persist();
            oB.m_bClosed = true;
            oB.abort("cancel");
            oB.m_oWeb.stopLoading();
            oB.m_oWeb.destroy();
            if (oB.m_oOverlay.getParent() != null)
                ((ViewGroup)oB.m_oOverlay.getParent()).removeView(oB.m_oOverlay);
            m_oInstance = null;
        }
    }
    private void abort(String strReason)
    {
        long nId = m_nJob;
        m_nJob = 0;
        if (nId != 0)
            nativeReply(m_nToken, nId, "{\"done\":true,\"error\":\"" + strReason + "\"}");
        if (!m_bClosed)
            m_oWeb.evaluateJavascript("if(window.__wenku8SdkJob){window.__wenku8SdkJob." +
                                      "controller.abort();delete window.__wenku8SdkJob;}",
                                      null);
    }
    public static void fetch(long nToken, long nRequest, String strScript)
    {
        ui(() ->
        {
            BrowserLogin oB = m_oInstance;
            if (oB == null || oB.m_bClosed || oB.m_nToken != nToken || !oB.m_bLoaded)
            {
                nativeReply(nToken, nRequest, "{\"done\":true,\"error\":\"navigation\"}");
                return;
            }
            oB.abort("cancel");
            oB.m_nJob = nRequest;
            oB.m_oWeb.evaluateJavascript(strScript, strV ->
            {
                if (oB.m_nJob != nRequest)
                    return;
                if (!"true".equals(strV))
                {
                    oB.abort("network");
                    return;
                }
                oB.poll(nRequest);
            });
        });
    }
    private void poll(long nId)
    {
        if (m_bClosed || m_nJob != nId)
            return;
        m_oWeb.evaluateJavascript(
            "(()=>{const s=window.__wenku8SdkJob;return " +
            "JSON.stringify(!s?{done:true,error:'navigation'}:s.done?{done:true,error:s.error," +
            "status:s.status,url:s.url,headers:s.headers,body:s.body}:{done:false});})()",
            strV ->
            {
                if (m_bClosed || m_nJob != nId)
                    return;
                try
                {
                    Object oDecoded = new JSONTokener(strV).nextValue();
                    if (!(oDecoded instanceof String))
                        throw new JSONException("reply");
                    String strText = (String)oDecoded;
                    JSONObject oResult = new JSONObject(strText);
                    if (oResult.optBoolean("done"))
                    {
                        m_nJob = 0;
                        nativeReply(m_nToken, nId, strText);
                    }
                    else
                        m_oHandler.postDelayed(() -> poll(nId), 25);
                }
                catch (Exception oE)
                {
                    abort("network");
                }
            });
    }
}
