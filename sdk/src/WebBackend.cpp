// SPDX-License-Identifier: GPL-2.0-only
// Independently implemented website adapter. Wire/page references: THIRD_PARTY_NOTICES.md.
#include "WebBackend.h"
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <regex>
#include <set>

namespace Wenku8::Detail
{
    namespace
    {
        std::string lower(std::string strValue)
        {
            for (char& chC : strValue)
                if (static_cast<unsigned char>(chC) < 128)
                    chC = static_cast<char>(std::tolower(static_cast<unsigned char>(chC)));
            return strValue;
        }
        std::string trim(std::string strS)
        {
            auto nBegin = strS.find_first_not_of(" \r\n\t");
            return nBegin == std::string::npos
                       ? ""
                       : strS.substr(nBegin, strS.find_last_not_of(" \r\n\t") - nBegin + 1);
        }
        std::string xmlString(xmlChar* pValue)
        {
            if (!pValue)
                return {};
            std::string strResult(reinterpret_cast<const char*>(pValue));
            xmlFree(pValue);
            return strResult;
        }
        std::string property(xmlNodePtr pNode, const char* pName)
        {
            return pNode ? xmlString(xmlGetProp(pNode, BAD_CAST pName)) : "";
        }
        std::string nodeText(xmlNodePtr pNode)
        {
            return pNode ? trim(xmlString(xmlNodeGetContent(pNode))) : "";
        }
        std::string header(const HttpResponse& oResponse, const std::string& strName)
        {
            for (const auto& oH : oResponse.m_vHeaders)
                if (lower(oH.first) == lower(strName))
                    return oH.second;
            return {};
        }
        void verification(const HttpResponse& oResponse)
        {
            const auto strSample = lower(oResponse.m_strBody.substr(0, 65536));
            // A Cloudflare block page may itself load challenge-platform telemetry.
            // Do not suggest interactive verification can resolve an explicit deny response.
            if ((strSample.find("<title>attention required! | cloudflare</title>") !=
                     std::string::npos &&
                 strSample.find("you have been blocked") != std::string::npos) ||
                (oResponse.m_nStatus == 403 && strSample.find("cloudflare") != std::string::npos &&
                 strSample.find("error code: 1020") != std::string::npos))
                throw Error(ErrorCode::SiteAccessDenied, "Website access blocked by Cloudflare",
                            oResponse.m_nStatus);
            // Cloudflare also injects /scripts/jsd/ into ordinary successful content.
            // That background detection script alone is not an interactive challenge.
            const bool bPlatform = strSample.find("challenge-platform") != std::string::npos;
            const bool bBackgroundDetection = strSample.find("/scripts/jsd/") != std::string::npos;
            if ((bPlatform && !bBackgroundDetection) ||
                strSample.find("__cf_chl_opt") != std::string::npos ||
                strSample.find("cf-chl-") != std::string::npos ||
                strSample.find("<title>just a moment") != std::string::npos)
                throw Error(ErrorCode::BrowserVerificationRequired,
                            "Website requires browser verification", oResponse.m_nStatus);
        }
        std::string decode(const HttpResponse& oResponse)
        {
            std::string strSample = lower(header(oResponse, "content-type") + " " +
                                          oResponse.m_strBody.substr(0, 4096));
            std::smatch oMatch;
            std::string strEncoding = "GBK";
            if (std::regex_search(strSample, oMatch,
                                  std::regex("charset\\s*=\\s*['\"]?\\s*([a-z0-9_-]+)")))
            {
                const auto strLabel = oMatch[1].str();
                if (strLabel == "utf-8" || strLabel == "utf8")
                    strEncoding = "UTF-8";
                else if (strLabel == "big5" || strLabel == "big5-hkscs")
                    strEncoding = "BIG5";
                else if (strLabel != "gbk" && strLabel != "gb2312" && strLabel != "gb18030")
                    throw Error(ErrorCode::Parse, "Unsupported website charset");
            }
            if (oResponse.m_strBody.compare(0, 3, "\xef\xbb\xbf") == 0)
                return oResponse.m_strBody.substr(3);
            return transcode(oResponse.m_strBody, strEncoding, "UTF-8");
        }
        struct UriDeleter
        {
            void operator()(xmlURI* pP) const
            {
                xmlFreeURI(pP);
            }
        };
        std::string originOf(const std::string& strUrl)
        {
            if (strUrl.find('\0') != std::string::npos ||
                strUrl.find_first_of("\r\n\t") != std::string::npos)
                throw Error(ErrorCode::InvalidArgument, "Invalid website URL");
            std::unique_ptr<xmlURI, UriDeleter> pUri(xmlParseURI(strUrl.c_str()));
            if (!pUri || !pUri->scheme || !pUri->server || pUri->user)
                throw Error(ErrorCode::InvalidArgument, "Invalid website origin");
            const auto strScheme = lower(pUri->scheme), strHost = lower(pUri->server);
            if (strScheme != "https" &&
                !(strScheme == "http" &&
                  (strHost == "127.0.0.1" || strHost == "localhost" || strHost == "[::1]")))
                throw Error(ErrorCode::InvalidArgument,
                            "Website requires HTTPS (HTTP allowed only on loopback)");
            std::string strResult = strScheme + "://" + strHost;
            if (pUri->port > 0 && !((strScheme == "https" && pUri->port == 443) ||
                                    (strScheme == "http" && pUri->port == 80)))
                strResult += ':' + std::to_string(pUri->port);
            return strResult;
        }
        std::string resolve(const std::string& strLink, const std::string& strBase)
        {
            auto strResult =
                xmlString(xmlBuildURI(BAD_CAST strLink.c_str(), BAD_CAST strBase.c_str()));
            if (strResult.empty())
                throw Error(ErrorCode::Parse, "Invalid website link");
            if (auto nFragment = strResult.find('#'); nFragment != std::string::npos)
                strResult.resize(nFragment);
            return strResult;
        }
        int numeric(const std::string& strS)
        {
            if (strS.empty() || strS.size() > 10 ||
                strS.find_first_not_of("0123456789") != std::string::npos)
                throw Error(ErrorCode::Parse, "Invalid website numeric field");
            auto nValue = std::stoull(strS);
            if (nValue > static_cast<unsigned>((std::numeric_limits<int>::max)()))
                throw Error(ErrorCode::Parse, "Website ID too large");
            return static_cast<int>(nValue);
        }
        int queryId(const std::string& strUrl, const std::string& strName)
        {
            std::smatch oMatch;
            return std::regex_search(strUrl, oMatch,
                                     std::regex("[?&]" + strName + "=([0-9]+)(?:[&#]|$)"))
                       ? numeric(oMatch[1].str())
                       : 0;
        }
        int chapterId(const std::string& strUrl)
        {
            std::smatch oMatch;
            return std::regex_search(strUrl, oMatch,
                                     std::regex("(?:^|/)([0-9]+)\\.html?(?:[?#].*)?$"))
                       ? numeric(oMatch[1].str())
                       : 0;
        }
        void positive(int nId)
        {
            if (nId <= 0)
                throw Error(ErrorCode::InvalidArgument, "ID/page must be positive");
        }
        class Html
        {
            std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> m_pDoc{nullptr, xmlFreeDoc};

          public:
            explicit Html(const std::string& strUtf8)
            {
                if (strUtf8.empty() ||
                    strUtf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                    throw Error(ErrorCode::Parse, "Invalid HTML size");
                m_pDoc.reset(htmlReadMemory(
                    strUtf8.data(), static_cast<int>(strUtf8.size()), nullptr, "UTF-8",
                    HTML_PARSE_RECOVER | HTML_PARSE_NONET | HTML_PARSE_NODEFDTD |
                        HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING));
                if (!m_pDoc)
                    throw Error(ErrorCode::Parse, "Cannot parse website HTML");
            }
            std::vector<xmlNodePtr> select(const char* pPath, xmlNodePtr pNode = nullptr) const
            {
                std::unique_ptr<xmlXPathContext, decltype(&xmlXPathFreeContext)> pCtx(
                    xmlXPathNewContext(m_pDoc.get()), xmlXPathFreeContext);
                if (!pCtx)
                    throw Error(ErrorCode::Parse, "Cannot create HTML query");
                pCtx->node = pNode ? pNode : xmlDocGetRootElement(m_pDoc.get());
                std::unique_ptr<xmlXPathObject, decltype(&xmlXPathFreeObject)> pResult(
                    xmlXPathEvalExpression(BAD_CAST pPath, pCtx.get()), xmlXPathFreeObject);
                std::vector<xmlNodePtr> vNodes;
                if (pResult && pResult->nodesetval)
                    for (int nI = 0; nI < pResult->nodesetval->nodeNr; ++nI)
                        vNodes.push_back(pResult->nodesetval->nodeTab[nI]);
                return vNodes;
            }
            xmlNodePtr one(const char* pPath, xmlNodePtr pNode = nullptr) const
            {
                auto vNodes = select(pPath, pNode);
                return vNodes.empty() ? nullptr : vNodes.front();
            }
        };
        void requireContent(const Html& oDoc, bool bRecognizedContent = false)
        {
            if (!bRecognizedContent &&
                oDoc.one("//form[.//input[@name='username'] and .//input[@name='password']]"))
                throw Error(ErrorCode::Authentication,
                            "Website returned a login form instead of content");
        }
        std::string plain(xmlNodePtr pNode, const std::string& strBase = {}, bool bImages = false)
        {
            if (!pNode)
                return {};
            if (pNode->type == XML_TEXT_NODE || pNode->type == XML_CDATA_SECTION_NODE)
                return pNode->content ? reinterpret_cast<const char*>(pNode->content) : "";
            const std::string strTag =
                pNode->name ? lower(reinterpret_cast<const char*>(pNode->name)) : "";
            if (strTag == "script" || strTag == "style")
                return {};
            if (strTag == "br")
                return "\n";
            if (strTag == "img")
            {
                auto strSrc = property(pNode, "src");
                return bImages && !strSrc.empty()
                           ? "\n<!--image-->" + resolve(strSrc, strBase) + "<!--image-->\n"
                           : "";
            }
            std::string strResult;
            for (auto pChild = pNode->children; pChild; pChild = pChild->next)
                strResult += plain(pChild, strBase, bImages);
            if (strTag == "p" || strTag == "div" || strTag == "tr")
                strResult += '\n';
            return strResult;
        }
        NovelPage parsePage(const std::string& strHtml)
        {
            Html oDoc(strHtml);
            NovelPage oResult;
            std::set<int> vIds;
            const auto pLinks =
                oDoc.one("//*[@id='content']")
                    ? "//*[@id='content']//a[contains(@href,'articleinfo.php') or "
                      "contains(@href,'reader.php') or contains(@href,'readbookcase.php') or "
                      "contains(@href,'/book/') or contains(@href,'/novel/')]"
                    : "//a[contains(@href,'articleinfo.php') or contains(@href,'reader.php') or "
                      "contains(@href,'readbookcase.php') or contains(@href,'/book/') or "
                      "contains(@href,'/novel/')]";
            for (auto pA : oDoc.select(pLinks))
            {
                auto strHref = property(pA, "href");
                int nId = queryId(strHref, "id");
                if (!nId && (strHref.find("reader.php") != std::string::npos ||
                             strHref.find("readbookcase.php") != std::string::npos))
                    nId = queryId(strHref, "aid");
                if (!nId)
                {
                    std::smatch oM;
                    if (std::regex_search(strHref, oM,
                                          std::regex("/book/([0-9]+)\\.html?(?:[?#].*)?$")) ||
                        std::regex_search(
                            strHref, oM,
                            std::regex(
                                "/(?:book|novel)/[0-9]+/([0-9]+)/index\\.html?(?:[?#].*)?$")))
                        nId = numeric(oM[1].str());
                }
                auto strTitle = nodeText(pA);
                if (nId > 0 && !strTitle.empty() && vIds.insert(nId).second)
                {
                    Novel oN;
                    oN.m_nId = nId;
                    oN.m_strTitle = strTitle;
                    oResult.m_vNovels.push_back(std::move(oN));
                }
            }
            requireContent(oDoc, !oResult.m_vNovels.empty());
            for (auto pA : oDoc.select("//a[contains(@href,'page=')]"))
                oResult.m_nPages =
                    std::max(oResult.m_nPages, queryId(property(pA, "href"), "page"));
            if (!oResult.m_vNovels.empty())
                oResult.m_nPages = std::max(1, oResult.m_nPages);
            else if (strHtml.find(u8"没有找到") == std::string::npos &&
                     strHtml.find(u8"没有相关") == std::string::npos &&
                     strHtml.find(u8"书架为空") == std::string::npos)
                throw Error(ErrorCode::Parse, "No recognized website novel list");
            return oResult;
        }
        std::vector<Novel> parseBookshelf(const std::string& strHtml)
        {
            Html oDoc(strHtml);
            // An empty authenticated shelf still has its own management form. Require
            // both controls so an arbitrary HTTP 200/login page is not treated as empty.
            const auto pForm =
                oDoc.one("//form[contains(@action,'bookcase.php') and .//input[@name='checkall'] "
                         "and .//select[@name='classlist']]");
            if (pForm && !oDoc.one("//input[@name='checkid[]']"))
            {
                requireContent(oDoc, true);
                return {};
            }
            return parsePage(strHtml).m_vNovels;
        }
        Novel parseNovel(const std::string& strHtml, int nId)
        {
            Html oDoc(strHtml);
            const bool bMetadata =
                (oDoc.one("//*[@id='content']//span[b]/b") || oDoc.one("//h1")) &&
                oDoc.one(u8"//td[contains(.,'小说作者：') and not(descendant::td)]");
            requireContent(oDoc, bMetadata);
            Novel oResult;
            oResult.m_nId = nId;
            for (const auto pPath :
                 {"//h1[1]", "//*[@id='content']//span[b][1]/b", "//*[@id='content']//b[1]"})
                if (oResult.m_strTitle.empty())
                    oResult.m_strTitle = nodeText(oDoc.one(pPath));
            if (oResult.m_strTitle.empty())
                throw Error(ErrorCode::Parse, "Missing website novel title");
            for (auto pCell : oDoc.select("//td[not(descendant::td)]"))
            {
                auto strText = trim(plain(pCell));
                const std::pair<const char*, std::string*> vLabels[] = {
                    {u8"小说作者：", &oResult.m_strAuthor},
                    {u8"最后更新：", &oResult.m_strLastUpdate},
                    {u8"文章状态：", &oResult.m_strStatus},
                    {u8"小说状态：", &oResult.m_strStatus},
                    {u8"文库分类：", &oResult.m_strPublisher},
                    {u8"内容简介：", &oResult.m_strIntro}};
                for (const auto& oLabel : vLabels)
                {
                    auto nAt = strText.find(oLabel.first);
                    if (nAt != std::string::npos && nAt < 24 && oLabel.second->empty())
                        *oLabel.second =
                            trim(strText.substr(nAt + std::string(oLabel.first).size()));
                }
            }
            for (auto pA : oDoc.select("//a[contains(@href,'.htm')]"))
            {
                auto strHref = property(pA, "href");
                std::smatch oMatch;
                int nCid =
                    std::regex_search(strHref, oMatch,
                                      std::regex("/(?:book|novel)/[0-9]+/" + std::to_string(nId) +
                                                 "/([0-9]+)\\.html?(?:[?#].*)?$"))
                        ? numeric(oMatch[1].str())
                        : 0;
                if (nCid > 0)
                {
                    oResult.m_nLatestChapterId = nCid;
                    oResult.m_strLatestChapter = nodeText(pA);
                }
            }
            return oResult;
        }
        std::vector<Volume> parseIndex(const std::string& strHtml)
        {
            Html oDoc(strHtml);
            requireContent(oDoc);
            std::vector<Volume> vResult;
            std::set<int> vIds;
            const bool bChapterCells =
                oDoc.one("//td[contains(concat(' ',normalize-space(@class),' '),' ccss ')]") !=
                nullptr;
            const auto pNodes =
                bChapterCells ? "//td[contains(concat(' ',normalize-space(@class),' '),' vcss ')] "
                                "| //td[contains(concat(' ',normalize-space(@class),' '),' ccss "
                                "')]//a[contains(@href,'.htm')]"
                              : "//td[contains(concat(' ',normalize-space(@class),' '),' vcss ')] "
                                "| //table//a[contains(@href,'.htm')]";
            for (auto pNode : oDoc.select(pNodes))
            {
                if (std::string(reinterpret_cast<const char*>(pNode->name)) == "td")
                {
                    Volume oV;
                    oV.m_strTitle = nodeText(pNode);
                    vResult.push_back(std::move(oV));
                    continue;
                }
                int nCid = chapterId(property(pNode, "href"));
                if (!nCid || !vIds.insert(nCid).second)
                    continue;
                if (vResult.empty())
                {
                    Volume oV;
                    oV.m_strTitle = u8"正文";
                    vResult.push_back(std::move(oV));
                }
                if (!vResult.back().m_nId)
                    vResult.back().m_nId =
                        nCid; // HTML supplies no API volume ID; use first chapter.
                vResult.back().m_vChapters.push_back({nCid, nodeText(pNode)});
            }
            vResult.erase(std::remove_if(vResult.begin(), vResult.end(),
                                         [](const Volume& oV)
                                         {
                                             return oV.m_vChapters.empty();
                                         }),
                          vResult.end());
            if (vResult.empty())
                throw Error(ErrorCode::Parse, "Missing website chapter index");
            return vResult;
        }
        UserProfile parseProfile(const std::string& strHtml)
        {
            Html oDoc(strHtml);
            requireContent(oDoc);
            UserProfile oProfile;
            for (auto pRow : oDoc.select("//tr"))
            {
                auto vCells = oDoc.select("./td", pRow);
                if (vCells.size() < 2)
                    continue;
                auto strLabel = nodeText(vCells[0]), strValue = nodeText(vCells[1]);
                strLabel.erase(std::remove_if(strLabel.begin(), strLabel.end(),
                                              [](unsigned char nC)
                                              {
                                                  return nC < 128 && std::isspace(nC);
                                              }),
                               strLabel.end());
                strLabel = lower(strLabel);
                if (strLabel.find(u8"用户id") != std::string::npos)
                    oProfile.m_nId = numeric(strValue);
                else if (strLabel.find(u8"用户名") != std::string::npos)
                    oProfile.m_strUsername = strValue;
                else if (strLabel.find(u8"昵称") != std::string::npos)
                    oProfile.m_strNickname = strValue;
                else if (strLabel.find(u8"积分") != std::string::npos)
                    oProfile.m_nScore = numeric(strValue);
                else if (strLabel.find(u8"经验") != std::string::npos)
                    oProfile.m_nExperience = numeric(strValue);
                else if (strLabel.find(u8"等级") != std::string::npos)
                    oProfile.m_strRank = strValue;
            }
            if (oProfile.m_nId <= 0 || oProfile.m_strUsername.empty())
                throw Error(ErrorCode::Authentication, "No authenticated website profile");
            return oProfile;
        }
    } // namespace

    struct WebBackend::Impl
    {
        ClientOptions m_oOptions;
        std::shared_ptr<HttpTransport> m_pTransport;
        std::unique_ptr<FileCache> m_pCache;
        std::string m_strOrigin, m_strLoginPath = "/login.php", m_strCaptchaUrl;
        Fields m_vHidden;
        bool m_bPrepared = false, m_bNeedsCaptcha = false;
        std::optional<User> m_oUser;
        std::string sameOrigin(const std::string& strLink, const std::string& strBase)
        {
            auto strUrl = resolve(strLink, strBase);
            if (originOf(strUrl) != m_strOrigin)
                throw Error(ErrorCode::InvalidArgument,
                            "Cross-origin website redirect/form rejected");
            return strUrl;
        }
        HttpResponse request(std::string strPath, std::string strMethod = "GET",
                             std::string strBody = {}, bool bAllowRetries = true)
        {
            HttpRequest oRequest{
                sameOrigin(strPath, m_strOrigin + '/'),
                strMethod,
                std::move(strBody),
                {{"User-Agent", m_oOptions.m_strUserAgent},
                 {"Accept", "text/html,application/xhtml+xml,image/*;q=0.8,*/*;q=0.5"},
                 {"Referer", m_strOrigin + m_strLoginPath}}};
            oRequest.m_bAllowRetries = bAllowRetries && strMethod == "GET";
            if (strMethod == "POST")
                oRequest.m_vHeaders.emplace_back("Content-Type",
                                                 "application/x-www-form-urlencoded");
            auto oTransfer = m_oOptions.m_oTransfer;
            oTransfer.m_bFollowRedirects = bAllowRetries && strMethod == "GET" &&
                                           m_pTransport->supportsSameOriginGetRedirects();
            for (int nAttempt = 0; nAttempt <= 5; ++nAttempt)
            {
                auto oResponse = m_pTransport->perform(oRequest, oTransfer);
                if (!oResponse.m_strEffectiveUrl.empty())
                    sameOrigin(oResponse.m_strEffectiveUrl, oRequest.m_strUrl);
                verification(oResponse);
                if (oResponse.m_nStatus >= 300 && oResponse.m_nStatus < 400)
                {
                    if (!bAllowRetries)
                        throw Error(ErrorCode::Http,
                                    "State-changing request redirected; verify resulting state",
                                    oResponse.m_nStatus);
                    // Never resend a credential-bearing POST. Only convert 302/303 to a same-origin
                    // GET.
                    if (oRequest.m_strMethod == "POST" && oResponse.m_nStatus != 302 &&
                        oResponse.m_nStatus != 303)
                        throw Error(ErrorCode::Http, "Unexpected POST redirect",
                                    oResponse.m_nStatus);
                    auto strNext = header(oResponse, "location");
                    if (strNext.empty())
                        throw Error(ErrorCode::Http, "Redirect without location",
                                    oResponse.m_nStatus);
                    oRequest.m_strUrl = sameOrigin(strNext, oRequest.m_strUrl);
                    oRequest.m_strMethod = "GET";
                    oRequest.m_strBody.clear();
                    continue;
                }
                if (oResponse.m_nStatus < 200 || oResponse.m_nStatus >= 300)
                    throw Error(ErrorCode::Http, "Website request failed", oResponse.m_nStatus);
                return oResponse;
            }
            throw Error(ErrorCode::Http, "Too many website redirects");
        }
        std::string page(const std::string& strPath)
        {
            return decode(request(strPath));
        }
        template <class Parse>
        auto fetch(const std::string& strPath, CachePolicy ePolicy, Parse oParse)
            -> decltype(oParse(std::string{}))
        {
            if (ePolicy != CachePolicy::NetworkOnly && ePolicy != CachePolicy::PreferCache &&
                ePolicy != CachePolicy::CacheOnly && ePolicy != CachePolicy::Refresh)
                throw Error(ErrorCode::InvalidArgument, "Invalid cache policy");
            const auto strKey = "website-utf8-v1\n" + m_strOrigin + strPath;
            if (m_pCache &&
                (ePolicy == CachePolicy::CacheOnly || ePolicy == CachePolicy::PreferCache))
            {
                if (auto oBytes = m_pCache->read(strKey, m_oOptions.m_oCacheMaxAge))
                {
                    try
                    {
                        return oParse(*oBytes);
                    }
                    catch (const Error& oE)
                    {
                        if (oE.code() != ErrorCode::Parse && oE.code() != ErrorCode::Authentication)
                            throw;
                    }
                }
            }
            if (ePolicy == CachePolicy::CacheOnly)
                throw Error(ErrorCode::CacheMiss, "No valid website cache entry");
            auto strHtml = page(strPath);
            auto oResult = oParse(strHtml);
            if (m_pCache && ePolicy != CachePolicy::NetworkOnly)
                m_pCache->write(strKey, strHtml);
            return oResult;
        }
        void prepare()
        {
            if (m_bPrepared)
                return;
            // Start credential login independently of any earlier authenticated session.
            // CaptchaRequired preserves this prepared anonymous session for the next call.
            m_oUser.reset();
            m_pTransport->clearCookies();
            m_strLoginPath = "/login.php";
            auto strHtml = page(m_strLoginPath);
            Html oDoc(strHtml);
            auto pForm =
                oDoc.one("//form[.//input[@name='username'] and .//input[@name='password']]");
            if (!pForm)
                throw Error(ErrorCode::Parse, "Website login form not found");
            auto strAction = property(pForm, "action");
            if (!strAction.empty())
                m_strLoginPath =
                    sameOrigin(strAction, m_strOrigin + m_strLoginPath).substr(m_strOrigin.size());
            m_vHidden.clear();
            for (auto pInput : oDoc.select(
                     ".//input[translate(@type,'HIDDEN','hidden')='hidden' and @name]", pForm))
                m_vHidden.emplace_back(property(pInput, "name"), property(pInput, "value"));
            m_bNeedsCaptcha = oDoc.one(".//input[@name='checkcode']", pForm) != nullptr;
            if (auto pImage = oDoc.one("//img[contains(@src,'checkcode.php')]"))
                m_strCaptchaUrl = sameOrigin(property(pImage, "src"), m_strOrigin + m_strLoginPath);
            else
                m_strCaptchaUrl = m_strOrigin + "/checkcode.php";
            m_bPrepared = true;
        }
    };
    WebBackend::WebBackend(ClientOptions oOptions, std::shared_ptr<HttpTransport> pTransport)
        : m_pImpl(new Impl)
    {
        m_pImpl->m_strOrigin = originOf(oOptions.m_strEndpoint);
        if (oOptions.m_strEndpoint != m_pImpl->m_strOrigin &&
            oOptions.m_strEndpoint != m_pImpl->m_strOrigin + '/')
            throw Error(ErrorCode::InvalidArgument,
                        "Website endpoint must be an origin without a path");
        if (oOptions.m_eLanguage != Language::Simplified)
            throw Error(ErrorCode::Unsupported,
                        "Website adapter currently returns simplified Chinese");
        m_pImpl->m_oOptions = std::move(oOptions);
        m_pImpl->m_pTransport = std::move(pTransport);
        if (!m_pImpl->m_oOptions.m_oCacheDirectory.empty())
            m_pImpl->m_pCache.reset(new FileCache(m_pImpl->m_oOptions.m_oCacheDirectory));
    }
    WebBackend::~WebBackend() = default;
    NovelPage WebBackend::list(Sort eSort, int nPage, CachePolicy ePolicy)
    {
        positive(nPage);
        static const char* vNames[] = {
            "allvisit", "allvote",  "monthvisit", "monthvote", "weekvisit", "weekvote", "dayvisit",
            "dayvote",  "postdate", "lastupdate", "goodnum",   "size",      "fullflag"};
        auto nIndex = static_cast<unsigned>(eSort);
        if (nIndex >= sizeof(vNames) / sizeof(vNames[0]))
            throw Error(ErrorCode::InvalidArgument, "Invalid sort");
        return m_pImpl->fetch("/modules/article/toplist.php?sort=" + std::string(vNames[nIndex]) +
                                  "&page=" + std::to_string(nPage) + "&charset=gbk",
                              ePolicy, parsePage);
    }
    NovelPage WebBackend::search(const std::string& strQuery, SearchType eType, CachePolicy ePolicy)
    {
        if (strQuery.empty() || (eType != SearchType::Title && eType != SearchType::Author))
            throw Error(ErrorCode::InvalidArgument, "Invalid search");
        auto strParams =
            formEncode({{"searchtype", eType == SearchType::Title ? "articlename" : "author"},
                        {"searchkey", transcode(strQuery, "UTF-8", "GBK")}});
        return m_pImpl->fetch("/modules/article/search.php?" + strParams, ePolicy, parsePage);
    }
    Novel WebBackend::novel(int nId, CachePolicy ePolicy)
    {
        positive(nId);
        return m_pImpl->fetch(
            "/modules/article/articleinfo.php?id=" + std::to_string(nId) + "&charset=gbk", ePolicy,
            [nId](const auto& oHtml)
            {
                return parseNovel(oHtml, nId);
            });
    }
    std::string WebBackend::introduction(int nId, CachePolicy ePolicy)
    {
        return novel(nId, ePolicy).m_strIntro;
    }
    std::vector<Volume> WebBackend::index(int nId, CachePolicy ePolicy)
    {
        positive(nId);
        return m_pImpl->fetch("/novel/" + std::to_string(nId / 1000) + '/' + std::to_string(nId) +
                                  "/index.htm",
                              ePolicy, parseIndex);
    }
    std::string WebBackend::chapter(int nId, int nCid, CachePolicy ePolicy)
    {
        positive(nId);
        positive(nCid);
        const auto strPath = "/novel/" + std::to_string(nId / 1000) + '/' + std::to_string(nId) +
                             '/' + std::to_string(nCid) + ".htm";
        return m_pImpl->fetch(
            strPath, ePolicy,
            [&](const auto& oHtml)
            {
                Html oDoc(oHtml);
                requireContent(oDoc);
                auto pContent = oDoc.one("//*[@id='content']");
                auto strResult = trim(plain(pContent, m_pImpl->m_strOrigin + strPath, true));
                if (strResult.empty())
                    throw Error(ErrorCode::Parse, "Website chapter content not found");
                return strResult;
            });
    }
    std::string WebBackend::cover(int nId, CachePolicy ePolicy)
    {
        positive(nId);
        if (ePolicy != CachePolicy::NetworkOnly && ePolicy != CachePolicy::PreferCache &&
            ePolicy != CachePolicy::CacheOnly && ePolicy != CachePolicy::Refresh)
            throw Error(ErrorCode::InvalidArgument, "Invalid cache policy");
        const auto strUrl = "https://img.wenku8.com/image/" + std::to_string(nId / 1000) + '/' +
                            std::to_string(nId) + '/' + std::to_string(nId) + "s.jpg";
        const auto strKey = "website-image-v1\n" + strUrl;
        auto fnJpeg = [](const std::string& strB)
        {
            return strB.size() >= 3 && static_cast<unsigned char>(strB[0]) == 0xff &&
                   static_cast<unsigned char>(strB[1]) == 0xd8;
        };
        if (m_pImpl->m_pCache &&
            (ePolicy == CachePolicy::PreferCache || ePolicy == CachePolicy::CacheOnly))
            if (auto oBytes = m_pImpl->m_pCache->read(strKey, m_pImpl->m_oOptions.m_oCacheMaxAge);
                oBytes && fnJpeg(*oBytes))
                return *oBytes;
        if (ePolicy == CachePolicy::CacheOnly)
            throw Error(ErrorCode::CacheMiss, "No cached website cover");
        auto oResponse = m_pImpl->m_pTransport->perform({strUrl, "GET", {}, {}},
                                                        m_pImpl->m_oOptions.m_oTransfer);
        verification(oResponse);
        if (oResponse.m_nStatus != 200)
            throw Error(ErrorCode::Http, "Cover request failed", oResponse.m_nStatus);
        if (!fnJpeg(oResponse.m_strBody))
            throw Error(ErrorCode::Parse, "Expected JPEG cover");
        if (m_pImpl->m_pCache && ePolicy != CachePolicy::NetworkOnly)
            m_pImpl->m_pCache->write(strKey, oResponse.m_strBody);
        return oResponse.m_strBody;
    }
    std::vector<Library> WebBackend::libraries(CachePolicy ePolicy)
    {
        return m_pImpl->fetch(
            "/modules/article/articlelist.php?charset=gbk", ePolicy,
            [](const auto& oHtml)
            {
                Html oDoc(oHtml);
                requireContent(oDoc);
                std::vector<Library> vResult;
                std::set<int> vSeen;
                for (auto pA : oDoc.select(
                         "//a[contains(@href,'articlelist.php') and contains(@href,'class=')]"))
                {
                    int nId = queryId(property(pA, "href"), "class");
                    if (nId > 0 && vSeen.insert(nId).second)
                        vResult.push_back({nId, nodeText(pA)});
                }
                if (vResult.empty())
                    throw Error(ErrorCode::Parse, "No website library categories");
                return vResult;
            });
    }
    NovelPage WebBackend::library(int nId, int nPage, CachePolicy ePolicy)
    {
        positive(nId);
        positive(nPage);
        return m_pImpl->fetch("/modules/article/articlelist.php?class=" + std::to_string(nId) +
                                  "&page=" + std::to_string(nPage) + "&charset=gbk",
                              ePolicy, parsePage);
    }
    LoginRequirements WebBackend::prepareLogin()
    {
        m_pImpl->prepare();
        return {m_pImpl->m_bNeedsCaptcha};
    }
    std::string WebBackend::captcha()
    {
        m_pImpl->prepare();
        auto oResponse = m_pImpl->request(m_pImpl->m_strCaptchaUrl);
        if (lower(header(oResponse, "content-type")).rfind("image/", 0) != 0)
            throw Error(ErrorCode::Parse, "Expected captcha image");
        return oResponse.m_strBody;
    }
    User WebBackend::login(const std::string& strName, const std::string& strPassword,
                           const std::string& strCaptcha)
    {
        if (strName.empty() || strPassword.empty())
            throw Error(ErrorCode::InvalidArgument, "Empty credentials");
        m_pImpl->m_oUser.reset();
        m_pImpl->prepare();
        if (m_pImpl->m_bNeedsCaptcha && strCaptcha.empty())
            throw Error(ErrorCode::CaptchaRequired, "Website requires a captcha");
        try
        {
            Fields vFields;
            for (const auto& oF : m_pImpl->m_vHidden)
                if (oF.first != "username" && oF.first != "password" && oF.first != "checkcode" &&
                    oF.first != "action" && oF.first != "usecookie" && oF.first != "submit")
                    vFields.push_back(oF);
            vFields.insert(vFields.end(), {{"action", "login"},
                                           {"username", transcode(strName, "UTF-8", "GBK")},
                                           {"password", transcode(strPassword, "UTF-8", "GBK")},
                                           {"usecookie", "0"},
                                           {"submit", transcode(u8"登录", "UTF-8", "GBK")}});
            if (!strCaptcha.empty())
                vFields.emplace_back("checkcode", strCaptcha);
            m_pImpl->request(m_pImpl->m_strLoginPath, "POST", formEncode(vFields));
            auto oProfile = parseProfile(m_pImpl->page("/userdetail.php?charset=gbk"));
            if (lower(oProfile.m_strUsername) != lower(strName))
                throw Error(ErrorCode::Authentication,
                            "Authenticated website account does not match requested username");
            m_pImpl->m_oUser = User{oProfile.m_nId, oProfile.m_strUsername};
            m_pImpl->m_bPrepared = false;
            return *m_pImpl->m_oUser;
        }
        catch (...)
        {
            m_pImpl->m_pTransport->clearCookies();
            m_pImpl->m_bPrepared = false;
            throw;
        }
    }
    UserProfile WebBackend::restoreSession(const std::string& strCookies)
    {
        m_pImpl->m_oUser.reset();
        m_pImpl->m_bPrepared = false;
        m_pImpl->m_pTransport->clearCookies();
        try
        {
            m_pImpl->m_pTransport->importCookies(m_pImpl->m_strOrigin, strCookies);
            auto oProfile = parseProfile(m_pImpl->page("/userdetail.php?charset=gbk"));
            m_pImpl->m_oUser = User{oProfile.m_nId, oProfile.m_strUsername};
            return oProfile;
        }
        catch (...)
        {
            m_pImpl->m_pTransport->clearCookies();
            throw;
        }
    }
    void WebBackend::logout()
    {
        m_pImpl->m_oUser.reset();
        m_pImpl->m_bPrepared = false;
        try
        {
            m_pImpl->request("/logout.php");
        }
        catch (...)
        {
            m_pImpl->m_pTransport->clearCookies();
            throw;
        }
        m_pImpl->m_pTransport->clearCookies();
    }
    std::optional<User> WebBackend::user() const
    {
        return m_pImpl->m_oUser;
    }
    UserProfile WebBackend::userInfo()
    {
        try
        {
            auto oProfile = parseProfile(m_pImpl->page("/userdetail.php?charset=gbk"));
            m_pImpl->m_oUser = User{oProfile.m_nId, oProfile.m_strUsername};
            return oProfile;
        }
        catch (const Error& oE)
        {
            if (oE.code() == ErrorCode::Authentication)
            {
                m_pImpl->m_oUser.reset();
                m_pImpl->m_pTransport->clearCookies();
            }
            throw;
        }
    }
    std::vector<Novel> WebBackend::bookshelf()
    {
        if (!m_pImpl->m_oUser)
            throw Error(ErrorCode::Authentication, "Website login required");
        // Use the site's canonical account URL. Decode the declared response charset
        // instead of adding a query parameter not present in its bookshelf link.
        return parseBookshelf(m_pImpl->page("/modules/article/bookcase.php"));
    }
    void WebBackend::addToBookshelf(int nId)
    {
        positive(nId);
        if (!m_pImpl->m_oUser)
            throw Error(ErrorCode::Authentication, "Website login required");
        auto fnContains = [nId](const std::vector<Novel>& vShelf)
        {
            return std::any_of(vShelf.begin(), vShelf.end(),
                               [nId](const Novel& oNovel)
                               {
                                   return oNovel.m_nId == nId;
                               });
        };
        if (fnContains(bookshelf()))
            return;
        std::exception_ptr oFailure;
        try
        {
            // The website uses a state-changing GET. It must never be automatically
            // replayed after an ambiguous response; always verify the resulting shelf.
            m_pImpl->request("/modules/article/addbookcase.php?bid=" + std::to_string(nId), "GET",
                             {}, false);
        }
        catch (const Error& oError)
        {
            if (oError.code() == ErrorCode::Authentication ||
                oError.code() == ErrorCode::BrowserVerificationRequired ||
                oError.code() == ErrorCode::SiteAccessDenied ||
                oError.code() == ErrorCode::Cancelled)
                throw;
            oFailure = std::current_exception();
        }
        if (fnContains(bookshelf()))
            return;
        if (oFailure)
            std::rethrow_exception(oFailure);
        throw Error(ErrorCode::Server, "Website did not confirm bookshelf addition");
    }

} // namespace Wenku8::Detail
