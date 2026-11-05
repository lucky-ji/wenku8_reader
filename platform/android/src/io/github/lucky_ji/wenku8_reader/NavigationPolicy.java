// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;
import java.net.URI;

/** Upgrade this site's legacy navigation without permitting plaintext login. */
public final class NavigationPolicy
{
    private final URI m_oOrigin;
    public NavigationPolicy(String strSite)
    {
        m_oOrigin = URI.create(strSite);
        if (!"https".equals(m_oOrigin.getScheme()) || m_oOrigin.getHost() == null ||
            m_oOrigin.getRawUserInfo() != null)
            throw new IllegalArgumentException("HTTPS origin required");
    }
    private int port(URI oUri)
    {
        return oUri.getPort() == -1 ? 443 : oUri.getPort();
    }
    public boolean same(String strRaw)
    {
        try
        {
            URI oUri = new URI(strRaw);
            return "https".equals(oUri.getScheme()) &&
                m_oOrigin.getHost().equalsIgnoreCase(oUri.getHost()) &&
                port(oUri) == port(m_oOrigin) && oUri.getRawUserInfo() == null;
        }
        catch (Exception oE)
        {
            return false;
        }
    }
    private boolean host(String strHost)
    {
        if (strHost == null)
            return false;
        if (m_oOrigin.getHost().equalsIgnoreCase(strHost))
            return true;
        return (m_oOrigin.getHost().equalsIgnoreCase("www.wenku8.net") &&
                strHost.equalsIgnoreCase("wenku8.net")) ||
            (m_oOrigin.getHost().equalsIgnoreCase("wenku8.net") &&
             strHost.equalsIgnoreCase("www.wenku8.net"));
    }
    public String canonical(String strRaw)
    {
        try
        {
            URI oUri = m_oOrigin.resolve(new URI(strRaw));
            if (!host(oUri.getHost()) || oUri.getRawUserInfo() != null)
                return null;
            if ("http".equals(oUri.getScheme()))
            {
                if ((oUri.getPort() != -1 && oUri.getPort() != 80) || port(m_oOrigin) != 443)
                    return null;
            }
            else if (!"https".equals(oUri.getScheme()) || port(oUri) != port(m_oOrigin))
                return null;
            String strPath = oUri.getRawPath();
            if (strPath == null || strPath.isEmpty())
                strPath = "/";
            return "https://" + m_oOrigin.getRawAuthority() + strPath +
                (oUri.getRawQuery() == null ? "" : "?" + oUri.getRawQuery()) +
                (oUri.getRawFragment() == null ? "" : "#" + oUri.getRawFragment());
        }
        catch (Exception oE)
        {
            return null;
        }
    }
}
