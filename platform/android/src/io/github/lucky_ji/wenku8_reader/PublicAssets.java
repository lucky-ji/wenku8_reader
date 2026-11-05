// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;
import java.net.*;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;
import java.io.*;
import java.util.*;

/**
 * Public images use platform TLS, never WebView cookies or exported trust stores. Called on SDK
 * workers.
 */
public final class PublicAssets
{
    // Field names form the JNI reply ABI consumed by AndroidAssetTransport.
    public static final class Reply
    {
        public int status = 0;
        public byte[] body = new byte[0];
        public String url = "", error = "";
        public String[] headerNames = new String[0], headerValues = new String[0];
    }
    private static native boolean continueTransfer(long nRequest, long nReceived, long nTotal);
    interface Progress
    {
        boolean proceed(long nReceived, long nTotal);
    }
    public static Reply get(long nRequest, String strAddress, int nConnectTimeout, int nTimeout,
                            int nLimit, boolean bFollow, String[] vNames, String[] vValues)
    {
        return request(strAddress, nConnectTimeout, nTimeout, nLimit, bFollow, vNames, vValues,
                       (nReceived, nTotal) -> continueTransfer(nRequest, nReceived, nTotal));
    }
    static Reply request(String strAddress, int nConnectTimeout, int nTimeout, int nLimit,
                         boolean bFollow, String[] vNames, String[] vValues, Progress fnProgress)
    {
        Reply oResult = new Reply();
        long nDeadline = System.nanoTime() + nTimeout * 1000000L;
        HttpsURLConnection oConnection = null;
        try
        {
            if (nTimeout <= 0 || nConnectTimeout <= 0 || nLimit <= 0 || nLimit > 32 * 1024 * 1024 ||
                vNames.length != vValues.length)
                throw new IllegalArgumentException();
            // WebView uses android.webkit.CookieManager, which is separate.
            // Do not silently inherit a future app-wide java.net cookie jar.
            if (CookieHandler.getDefault() != null)
            {
                oResult.error = "session";
                return oResult;
            }
            for (int nRedirect = 0;; nRedirect++)
            {
                if (!PublicAssetPolicy.allowed(strAddress))
                {
                    oResult.error = "origin";
                    return oResult;
                }
                if (!fnProgress.proceed(0, 0))
                {
                    oResult.error = "cancel";
                    return oResult;
                }
                int nRemaining = (int)Math.min(
                    Integer.MAX_VALUE, Math.max(0, (nDeadline - System.nanoTime()) / 1000000L));
                if (nRemaining <= 0)
                {
                    oResult.error = "timeout";
                    return oResult;
                }
                oConnection = (HttpsURLConnection) new URL(strAddress).openConnection();
                oConnection.setInstanceFollowRedirects(false);
                oConnection.setUseCaches(false);
                oConnection.setAllowUserInteraction(false);
                oConnection.setRequestMethod("GET");
                oConnection.setConnectTimeout(Math.min(nConnectTimeout, nRemaining));
                oConnection.setReadTimeout(Math.min(nRemaining, nConnectTimeout));
                oConnection.setRequestProperty("Accept-Encoding", "identity");
                oConnection.setRequestProperty("Accept", "image/jpeg,image/png,image/*;q=0.8");
                oConnection.setRequestProperty("User-Agent",
                                               "Wenku8Reader/1.0.0 (Android; public images)");
                for (int nI = 0; nI < vNames.length; nI++)
                {
                    String strName = vNames[nI].toLowerCase(Locale.ROOT), strValue = vValues[nI];
                    if (!Arrays.asList("accept", "user-agent", "range", "if-range")
                             .contains(strName) ||
                        strValue.indexOf('\r') >= 0 || strValue.indexOf('\n') >= 0 ||
                        strValue.indexOf(0) >= 0)
                    {
                        oResult.error = "header";
                        return oResult;
                    }
                    oConnection.setRequestProperty(strName, strValue);
                }
                oResult.status = oConnection.getResponseCode();
                oResult.url = strAddress;
                oConnection.setReadTimeout(Math.min(nRemaining, 1000));
                if (bFollow && Arrays.asList(301, 302, 303, 307, 308).contains(oResult.status))
                {
                    String strDestination = oConnection.getHeaderField("Location");
                    if (strDestination == null || nRedirect >= 3)
                    {
                        oResult.error = "redirect";
                        return oResult;
                    }
                    strAddress = new URL(new URL(strAddress), strDestination).toExternalForm();
                    oConnection.disconnect();
                    oConnection = null;
                    continue;
                }
                ArrayList<String> vHeaderNames = new ArrayList<>(), vHeaderValues =
                                                                        new ArrayList<>();
                for (String strName :
                     Arrays.asList("Content-Type", "Content-Length", "Content-Range", "ETag",
                                   "Last-Modified", "Accept-Ranges", "Location", "Retry-After"))
                {
                    String strValue = oConnection.getHeaderField(strName);
                    if (strValue != null)
                    {
                        vHeaderNames.add(strName);
                        vHeaderValues.add(strValue);
                    }
                }
                oResult.headerNames = vHeaderNames.toArray(new String[0]);
                oResult.headerValues = vHeaderValues.toArray(new String[0]);
                long nTotal = 0;
                try
                {
                    nTotal = Long.parseLong(oConnection.getHeaderField("Content-Length"));
                }
                catch (Exception oIgnored)
                {
                }
                if (nTotal > nLimit)
                {
                    oResult.error = "size";
                    return oResult;
                }
                InputStream oStream = oResult.status >= 400 ? oConnection.getErrorStream()
                                                            : oConnection.getInputStream();
                if (oStream == null)
                    return oResult;
                ByteArrayOutputStream oBytes = new ByteArrayOutputStream(Math.min(nLimit, 16384));
                byte[] vBuffer = new byte[16384];
                try (InputStream oInput = oStream)
                {
                    for (;;)
                    {
                        if (System.nanoTime() >= nDeadline)
                        {
                            oResult.error = "timeout";
                            return oResult;
                        }
                        if (!fnProgress.proceed(oBytes.size(), Math.max(0, nTotal)))
                        {
                            oResult.error = "cancel";
                            return oResult;
                        }
                        int nRead;
                        try
                        {
                            nRead = oInput.read(vBuffer);
                        }
                        catch (SocketTimeoutException oE)
                        {
                            continue;
                        }
                        if (nRead < 0)
                            break;
                        if (nRead > nLimit - oBytes.size())
                        {
                            oResult.error = "size";
                            return oResult;
                        }
                        oBytes.write(vBuffer, 0, nRead);
                    }
                }
                oResult.body = oBytes.toByteArray();
                return oResult;
            }
        }
        catch (SSLException oE)
        {
            oResult.error = "tls";
        }
        catch (SocketTimeoutException oE)
        {
            oResult.error = "timeout";
        }
        catch (Exception oE)
        {
            oResult.error = "network";
        }
        finally
        {
            if (oConnection != null)
                oConnection.disconnect();
        }
        return oResult;
    }
}
