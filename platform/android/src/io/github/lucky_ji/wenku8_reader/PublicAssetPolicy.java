// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;
import java.net.URI;
public final class PublicAssetPolicy
{
    public static boolean allowed(String strValue)
    {
        try
        {
            URI oU = new URI(strValue);
            String strH = oU.getHost();
            return "https".equals(oU.getScheme()) && oU.getRawUserInfo() == null &&
                oU.getRawFragment() == null && (oU.getPort() == -1 || oU.getPort() == 443) &&
                ("img.wenku8.com".equalsIgnoreCase(strH) ||
                 "pic.wenku8.com".equalsIgnoreCase(strH) ||
                 "pic.777743.xyz".equalsIgnoreCase(strH));
        }
        catch (Exception oE)
        {
            return false;
        }
    }
}
