// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.app.Activity;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Handler;
import android.os.Looper;
import android.webkit.*;
import java.io.ByteArrayInputStream;
import org.json.*;

/** Independent, noninteractive request document; CookieManager remains browser-owned. */
final class DownloadBrowser
{
    interface Reply
    {
        void result(long nRequest, String strJson);
    }
    private final WebView m_oWeb;
    private final Handler m_oHandler = new Handler(Looper.getMainLooper());
    private final NavigationPolicy m_oNavigation;
    private final String m_strOrigin;
    private final Reply m_fnReply;
    private long m_nJob = 0, m_nGeneration = 0;
    private boolean m_bClosed = false, m_bLoaded = false, m_bLoading = false;
    private String m_strScript = "";
    DownloadBrowser(Activity oActivity, String strOrigin, String strAgent, Reply fnReply)
    {
        m_strOrigin = strOrigin;
        m_oNavigation = new NavigationPolicy(strOrigin);
        m_fnReply = fnReply;
        // Never attach this WebView to a window or mutate the Qt content surface.
        m_oWeb = new WebView(oActivity);
        WebSettings oSettings = m_oWeb.getSettings();
        oSettings.setJavaScriptEnabled(true);
        oSettings.setDomStorageEnabled(false);
        oSettings.setSavePassword(false);
        oSettings.setSaveFormData(false);
        oSettings.setAllowFileAccess(false);
        oSettings.setAllowContentAccess(false);
        oSettings.setAllowFileAccessFromFileURLs(false);
        oSettings.setAllowUniversalAccessFromFileURLs(false);
        oSettings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        oSettings.setJavaScriptCanOpenWindowsAutomatically(false);
        oSettings.setSupportMultipleWindows(false);
        oSettings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        oSettings.setUserAgentString(strAgent);
        if (android.os.Build.VERSION.SDK_INT >= 26)
        {
            oSettings.setSafeBrowsingEnabled(true);
        }
        CookieManager.getInstance().setAcceptThirdPartyCookies(m_oWeb, false);
        m_oWeb.setWebChromeClient(new WebChromeClient()
        {
            @Override
            public boolean onConsoleMessage(ConsoleMessage oMessage)
            {
                return true;
            }
            @Override
            public void onPermissionRequest(PermissionRequest oRequest)
            {
                oRequest.deny();
            }
            @Override
            public boolean onShowFileChooser(WebView oView, ValueCallback<Uri[]> fnResult,
                                             FileChooserParams oParameters)
            {
                fnResult.onReceiveValue(null);
                return true;
            }
            @Override
            public boolean onJsAlert(WebView oView, String strUrl, String strMessage, JsResult oResult)
            {
                oResult.cancel();
                return true;
            }
            @Override
            public boolean onJsConfirm(WebView oView, String strUrl, String strMessage, JsResult oResult)
            {
                oResult.cancel();
                return true;
            }
            @Override
            public boolean onJsPrompt(WebView oView, String strUrl, String strMessage,
                                      String strDefault, JsPromptResult oResult)
            {
                oResult.cancel();
                return true;
            }
        });
        m_oWeb.setWebViewClient(new WebViewClient()
        {
            @Override
            public boolean shouldOverrideUrlLoading(WebView oView, WebResourceRequest oRequest)
            {
                return !"GET".equals(oRequest.getMethod()) ||
                       !m_oNavigation.same(oRequest.getUrl().toString());
            }
            @Override
            public boolean shouldOverrideUrlLoading(WebView oView, String strUrl)
            {
                return !m_oNavigation.same(strUrl);
            }
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView oView, WebResourceRequest oRequest)
            {
                if (!resource(oRequest.getUrl().toString()) || !"GET".equals(oRequest.getMethod()))
                {
                    return new WebResourceResponse("text/plain", "utf-8", 403, "Blocked",
                            java.util.Collections.emptyMap(), new ByteArrayInputStream(new byte[0]));
                }
                return null;
            }
            @Override
            public void onReceivedSslError(WebView oView, SslErrorHandler oHandler, SslError oError)
            {
                oHandler.cancel();
                failed("network");
            }
            @android.annotation.TargetApi(23)
            @Override
            public void onReceivedError(WebView oView, WebResourceRequest oRequest, WebResourceError oError)
            {
                if (oRequest.isForMainFrame())
                {
                    failed("network");
                }
            }
            @SuppressWarnings("deprecation")
            @Override
            public void onReceivedError(WebView oView, int nCode, String strDescription, String strUrl)
            {
                if (android.os.Build.VERSION.SDK_INT < 23 && strUrl.equals(oView.getUrl()))
                {
                    failed("network");
                }
            }
            @Override
            public void onReceivedHttpError(WebView oView, WebResourceRequest oRequest,
                                            WebResourceResponse oResponse)
            {
                if (oRequest.isForMainFrame())
                {
                    httpFailure(oResponse.getStatusCode(), oRequest.getUrl().toString());
                }
            }
            @Override
            public void onPageStarted(WebView oView, String strUrl, android.graphics.Bitmap oIcon)
            {
                m_bLoaded = false;
                if (!m_bLoading)
                {
                    failed("navigation");
                }
            }
            @Override
            public void onPageFinished(WebView oView, String strUrl)
            {
                if (m_bClosed || !m_bLoading || !strUrl.equals(oView.getUrl()))
                {
                    return;
                }
                if (!m_oNavigation.same(strUrl))
                {
                    failed("navigation");
                    return;
                }
                final long nGeneration = m_nGeneration;
                oView.evaluateJavascript("(()=>{if(location.origin!==" + JSONObject.quote(m_strOrigin) +
                    ")return 'network';return document.querySelector('input[name=username]')&&" +
                    "document.querySelector('input[name=password]')?'auth':'ready';})()", strValue ->
                {
                    if (m_bClosed || !m_bLoading || nGeneration != m_nGeneration)
                    {
                        return;
                    }
                    if ("\"auth\"".equals(strValue))
                    {
                        httpFailure(401, strUrl);
                    }
                    else if ("\"ready\"".equals(strValue))
                    {
                        m_bLoading = false;
                        m_bLoaded = true;
                        execute();
                    }
                    else
                    {
                        failed("network");
                    }
                });
            }
            @Override
            public boolean onRenderProcessGone(WebView oView, RenderProcessGoneDetail oDetail)
            {
                failed("network");
                close();
                return true;
            }
        });
    }
    private boolean resource(String strUrl)
    {
        Uri oUri = Uri.parse(strUrl);
        String strHost = oUri.getHost();
        return m_oNavigation.same(strUrl) || "about:blank".equals(strUrl) ||
            ("https".equals(oUri.getScheme()) && oUri.getUserInfo() == null &&
             (oUri.getPort() == -1 || oUri.getPort() == 443) &&
             ("challenges.cloudflare.com".equals(strHost) || "img.wenku8.com".equals(strHost)));
    }
    private void httpFailure(int nStatus, String strUrl)
    {
        if (!m_oNavigation.same(strUrl))
        {
            failed("navigation");
            return;
        }
        long nRequest = m_nJob;
        m_nJob = 0;
        m_strScript = "";
        m_bLoaded = false;
        m_bLoading = false;
        ++m_nGeneration;
        if (nRequest != 0)
        {
            m_fnReply.result(nRequest, "{\"done\":true,\"status\":" + nStatus +
                ",\"url\":" + JSONObject.quote(strUrl) + ",\"headers\":[],\"body\":\"\"}");
        }
    }
    void fetch(long nRequest, String strScript)
    {
        if (m_bClosed)
        {
            replyError(nRequest, "network");
            return;
        }
        abort(m_nJob, "cancel");
        m_nJob = nRequest;
        m_strScript = strScript;
        if (m_bLoaded)
        {
            execute();
            return;
        }
        m_bLoading = true;
        final long nGeneration = ++m_nGeneration;
        m_oWeb.loadUrl(m_strOrigin + "/userdetail.php?charset=gbk");
        m_oHandler.postDelayed(() ->
        {
            if (!m_bClosed && m_bLoading && nGeneration == m_nGeneration)
            {
                failed("network");
            }
        }, 15000);
    }
    private void execute()
    {
        final long nRequest = m_nJob;
        if (nRequest == 0 || m_bClosed || !m_bLoaded)
        {
            return;
        }
        m_oWeb.evaluateJavascript(m_strScript, strValue ->
        {
            if (m_bClosed || m_nJob != nRequest)
            {
                return;
            }
            if (!"true".equals(strValue))
            {
                failed("network");
                return;
            }
            poll(nRequest);
        });
    }
    private void poll(long nRequest)
    {
        if (m_bClosed || m_nJob != nRequest)
        {
            return;
        }
        m_oWeb.evaluateJavascript(
            "(()=>{const s=window.__wenku8SdkJob;return " +
            "JSON.stringify(!s?{done:true,error:'navigation'}:s.done?{done:true,error:s.error," +
            "status:s.status,url:s.url,headers:s.headers,body:s.body}:{done:false});})()", strValue ->
        {
            if (m_bClosed || m_nJob != nRequest)
            {
                return;
            }
            try
            {
                Object oDecoded = new JSONTokener(strValue).nextValue();
                if (!(oDecoded instanceof String))
                {
                    throw new JSONException("reply");
                }
                String strJson = (String)oDecoded;
                JSONObject oResult = new JSONObject(strJson);
                if (oResult.optBoolean("done"))
                {
                    m_nJob = 0;
                    m_strScript = "";
                    m_fnReply.result(nRequest, strJson);
                }
                else
                {
                    m_oHandler.postDelayed(() -> poll(nRequest), 50);
                }
            }
            catch (Exception oError)
            {
                failed("network");
            }
        });
    }
    private void replyError(long nRequest, String strReason)
    {
        if (nRequest != 0)
        {
            m_fnReply.result(nRequest, "{\"done\":true,\"error\":" + JSONObject.quote(strReason) + "}");
        }
    }
    void abort(long nRequest, String strReason)
    {
        if (nRequest == 0 || nRequest != m_nJob)
        {
            return;
        }
        m_nJob = 0;
        m_strScript = "";
        if (m_bLoading)
        {
            m_bLoading = false;
            ++m_nGeneration;
            m_oWeb.stopLoading();
        }
        if (!m_bClosed)
        {
            m_oWeb.evaluateJavascript("if(window.__wenku8SdkJob){window.__wenku8SdkJob." +
                "controller.abort();delete window.__wenku8SdkJob;}", null);
        }
        replyError(nRequest, strReason);
    }
    private void failed(String strReason)
    {
        m_bLoaded = false;
        m_bLoading = false;
        ++m_nGeneration;
        abort(m_nJob, strReason);
    }
    void close()
    {
        if (m_bClosed)
        {
            return;
        }
        failed("cancel");
        m_bClosed = true;
        m_oHandler.removeCallbacksAndMessages(null);
        m_oWeb.stopLoading();
        m_oWeb.destroy();
    }
}
