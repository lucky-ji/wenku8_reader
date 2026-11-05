// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Catalog.h"
#include "tinyxml2.h"
#include <libxml/uri.h>
#include <libxml/xmlmemory.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <set>

namespace Wenku8
{
    namespace
    {
        constexpr const char* pAtom = "http://www.w3.org/2005/Atom";
        constexpr const char* pOpenSearch = "http://a9.com/-/spec/opensearch/1.1/";
        std::string lower(std::string strText)
        {
            for (char& chC : strText)
                if (static_cast<unsigned char>(chC) < 128)
                    chC = static_cast<char>(std::tolower(static_cast<unsigned char>(chC)));
            return strText;
        }
        std::string attribute(const tinyxml2::XMLElement* pE, const char* pName)
        {
            auto pValue = pE->Attribute(pName);
            return pValue ? pValue : "";
        }
        std::string text(const tinyxml2::XMLElement* pE)
        {
            if (!pE)
                return {};
            std::string strResult;
            for (auto pNode = pE->FirstChild(); pNode; pNode = pNode->NextSibling())
            {
                if (auto pT = pNode->ToText())
                    strResult += pT->Value();
                else if (auto pChild = pNode->ToElement())
                    strResult += text(pChild);
            }
            return strResult;
        }
        bool named(const tinyxml2::XMLElement* pE, const std::string& strLocal,
                   const std::string& strNs)
        {
            std::string strName = pE->Name();
            auto nColon = strName.find(':');
            const auto strPrefix = nColon == std::string::npos ? "" : strName.substr(0, nColon);
            if ((nColon == std::string::npos ? strName : strName.substr(nColon + 1)) != strLocal)
                return false;
            const auto strDeclaration = strPrefix.empty() ? "xmlns" : "xmlns:" + strPrefix;
            for (auto pNode = pE; pNode;
                 pNode = pNode->Parent() ? pNode->Parent()->ToElement() : nullptr)
                if (const auto pValue = pNode->Attribute(strDeclaration.c_str()))
                    return strNs == pValue;
            return false;
        }
        const tinyxml2::XMLElement* child(const tinyxml2::XMLElement* pE,
                                          const std::string& strName)
        {
            for (auto pC = pE->FirstChildElement(); pC; pC = pC->NextSiblingElement())
                if (named(pC, strName, pAtom))
                    return pC;
            return nullptr;
        }
        const tinyxml2::XMLElement* parse(tinyxml2::XMLDocument& oDoc, const std::string& strXml,
                                          const char* pRoot, const char* pNs)
        {
            if (strXml.empty() || strXml.find('\0') != std::string::npos ||
                oDoc.Parse(strXml.data(), strXml.size()) != tinyxml2::XML_SUCCESS ||
                !oDoc.RootElement() || oDoc.RootElement()->NextSiblingElement() ||
                !named(oDoc.RootElement(), pRoot, pNs))
                throw Error(ErrorCode::Parse, "Unexpected catalog XML document");
            for (auto pN = oDoc.FirstChild(); pN; pN = pN->NextSibling())
                if (pN->ToUnknown())
                    throw Error(ErrorCode::Parse, "Catalog DTD is unsupported");
            return oDoc.RootElement();
        }
        struct UriDeleter
        {
            void operator()(xmlURI* pUri) const
            {
                xmlFreeURI(pUri);
            }
        };
        std::string origin(const std::string& strUrl)
        {
            if (strUrl.empty() || strUrl.size() > 16384 ||
                std::any_of(strUrl.begin(), strUrl.end(),
                            [](unsigned char nC)
                            {
                                return nC <= 32 || nC == 127;
                            }))
                throw Error(ErrorCode::InvalidArgument, "Invalid catalog URL");
            std::unique_ptr<xmlURI, UriDeleter> pUri(xmlParseURI(strUrl.c_str()));
            if (!pUri || !pUri->server || !pUri->scheme || pUri->user || pUri->fragment)
                throw Error(ErrorCode::InvalidArgument, "Invalid catalog URL authority");
            const auto strScheme = lower(pUri->scheme), strHost = lower(pUri->server);
            if (strScheme != "https" &&
                !(strScheme == "http" &&
                  (strHost == "localhost" || strHost == "127.0.0.1" || strHost == "[::1]")))
                throw Error(ErrorCode::InvalidArgument,
                            "Catalog requires HTTPS (HTTP allowed on loopback only)");
            auto strResult = strScheme + "://" + strHost;
            if (pUri->port > 0 && !((strScheme == "https" && pUri->port == 443) ||
                                    (strScheme == "http" && pUri->port == 80)))
                strResult += ':' + std::to_string(pUri->port);
            return strResult;
        }
        std::string resolve(const std::string& strRef, const std::string& strBase)
        {
            auto pResolved = xmlBuildURI(BAD_CAST strRef.c_str(), BAD_CAST strBase.c_str());
            if (!pResolved)
                throw Error(ErrorCode::Parse, "Invalid catalog reference");
            std::string strResult(reinterpret_cast<const char*>(pResolved));
            xmlFree(pResolved);
            origin(strResult);
            return strResult;
        }
        std::string header(const HttpResponse& oResponse, const char* pName)
        {
            for (const auto& oH : oResponse.m_vHeaders)
                if (lower(oH.first) == pName)
                    return oH.second;
            return {};
        }
        bool atomType(const std::string& strType)
        {
            return lower(strType).rfind("application/atom+xml", 0) == 0;
        }
        int articleId(const std::string& strUrn)
        {
            const std::string strPrefix = "urn:wenku8articleid:";
            if (strUrn.rfind(strPrefix, 0) != 0)
                throw Error(ErrorCode::Parse, "Catalog book has no Wenku8 identity");
            int nId = 0;
            const auto pFirst = strUrn.data() + strPrefix.size(),
                       pLast = strUrn.data() + strUrn.size();
            auto oResult = std::from_chars(pFirst, pLast, nId);
            if (oResult.ec != std::errc{} || oResult.ptr != pLast || nId <= 0)
                throw Error(ErrorCode::Parse, "Invalid catalog article ID");
            return nId;
        }
    } // namespace
    struct CatalogClient::Impl
    {
        CatalogOptions m_oOptions;
        std::string m_strSourceOrigin;
        std::shared_ptr<HttpTransport> m_pTransport;
        std::unique_ptr<FileCache> m_pCache;
        std::string sameOrigin(const std::string& strRef, const std::string& strBase) const
        {
            auto strUrl = resolve(strRef, strBase);
            if (origin(strUrl) != m_strSourceOrigin)
                throw Error(ErrorCode::InvalidArgument, "Cross-origin catalog navigation rejected");
            return strUrl;
        }
        std::string baseOf(const tinyxml2::XMLElement* pNode, const std::string& strUrl) const
        {
            std::vector<std::string> vBases;
            for (auto pE = pNode; pE; pE = pE->Parent() ? pE->Parent()->ToElement() : nullptr)
                if (auto pValue = pE->Attribute("xml:base"))
                    vBases.emplace_back(pValue);
            auto strBase = strUrl;
            for (auto oIt = vBases.rbegin(); oIt != vBases.rend(); ++oIt)
                strBase = sameOrigin(*oIt, strBase);
            return strBase;
        }
        CatalogLink link(const tinyxml2::XMLElement* pElement, const std::string& strUrl) const
        {
            CatalogLink oResult;
            oResult.m_strRelation = attribute(pElement, "rel");
            oResult.m_strMediaType = attribute(pElement, "type");
            auto strHref = attribute(pElement, "href");
            if (strHref.empty())
                throw Error(ErrorCode::Parse, "Catalog link lacks href");
            oResult.m_strUrl = resolve(strHref, baseOf(pElement, strUrl));
            return oResult;
        }
        CatalogPage page(const std::string& strXml, const std::string& strUrl) const
        {
            tinyxml2::XMLDocument oDoc;
            auto pRoot = parse(oDoc, strXml, "feed", pAtom);
            CatalogPage oPage;
            oPage.m_strTitle = text(child(pRoot, "title"));
            oPage.m_strUrl = strUrl;
            if (oPage.m_strTitle.empty())
                throw Error(ErrorCode::Parse, "Catalog lacks title");
            for (auto pE = pRoot->FirstChildElement(); pE; pE = pE->NextSiblingElement())
            {
                if (named(pE, "link", pAtom))
                {
                    const auto strRelation = attribute(pE, "rel");
                    if (strRelation == "next" || strRelation == "previous" ||
                        strRelation == "prev" || strRelation == "search")
                    {
                        auto oL = link(pE, strUrl);
                        oL.m_strUrl = sameOrigin(oL.m_strUrl, strUrl);
                        if (strRelation == "next")
                            oPage.m_strNextUrl = oL.m_strUrl;
                        else if (strRelation == "previous" || strRelation == "prev")
                            oPage.m_strPreviousUrl = oL.m_strUrl;
                        else if (lower(oL.m_strMediaType) ==
                                 "application/opensearchdescription+xml")
                            oPage.m_strSearchDescriptionUrl = oL.m_strUrl;
                    }
                    continue;
                }
                if (!named(pE, "entry", pAtom))
                    continue;
                const auto strTitle = text(child(pE, "title"));
                std::vector<CatalogLink> vAcquisitions;
                for (auto pN = pE->FirstChildElement(); pN; pN = pN->NextSiblingElement())
                    if (named(pN, "link", pAtom))
                    {
                        const auto strType = attribute(pN, "type"),
                                   strRelation = attribute(pN, "rel");
                        if (atomType(strType))
                        {
                            auto oL = link(pN, strUrl);
                            if (origin(oL.m_strUrl) == m_strSourceOrigin)
                            {
                                oL.m_strTitle = strTitle;
                                oPage.m_vCollections.push_back(std::move(oL));
                            }
                        }
                        else if (strRelation.rfind("http://opds-spec.org/acquisition", 0) == 0 ||
                                 strRelation == "http://opds-spec.org/image" ||
                                 strRelation == "http://opds-spec.org/image/thumbnail")
                        {
                            auto oL = link(pN, strUrl);
                            oL.m_strTitle = strTitle;
                            vAcquisitions.push_back(std::move(oL));
                        }
                    }
                const bool bIsBook = std::any_of(
                    vAcquisitions.begin(), vAcquisitions.end(),
                    [](const CatalogLink& oL)
                    {
                        return oL.m_strRelation.rfind("http://opds-spec.org/acquisition", 0) == 0;
                    });
                if (!bIsBook)
                    continue;
                if (strTitle.empty())
                    throw Error(ErrorCode::Parse, "Catalog book lacks title");
                CatalogBook oBook;
                oBook.m_strEntryId = text(child(pE, "id"));
                oBook.m_oNovel.m_nId = articleId(oBook.m_strEntryId);
                oBook.m_oNovel.m_strTitle = strTitle;
                oBook.m_oNovel.m_strLastUpdate = text(child(pE, "updated"));
                for (auto pN = pE->FirstChildElement(); pN; pN = pN->NextSiblingElement())
                    if (named(pN, "author", pAtom))
                    {
                        auto strName = text(child(pN, "name"));
                        if (!strName.empty())
                        {
                            if (!oBook.m_oNovel.m_strAuthor.empty())
                                oBook.m_oNovel.m_strAuthor += ", ";
                            oBook.m_oNovel.m_strAuthor += strName;
                        }
                    }
                // Preserve only text summaries in Novel::intro; do not expose HTML as plain text.
                if (auto pSummary = child(pE, "summary");
                    pSummary &&
                    (attribute(pSummary, "type").empty() || attribute(pSummary, "type") == "text"))
                    oBook.m_oNovel.m_strIntro = text(pSummary);
                oBook.m_vLinks = std::move(vAcquisitions);
                oPage.m_vBooks.push_back(std::move(oBook));
            }
            return oPage;
        }
        template <class Parse>
        auto fetch(const std::string& strUrl, CachePolicy ePolicy, Parse oParse)
            -> decltype(oParse(std::string{}, std::string{}))
        {
            if (ePolicy != CachePolicy::NetworkOnly && ePolicy != CachePolicy::PreferCache &&
                ePolicy != CachePolicy::Refresh && ePolicy != CachePolicy::CacheOnly)
                throw Error(ErrorCode::InvalidArgument, "Invalid catalog cache policy");
            const auto strInitial = sameOrigin(strUrl, m_oOptions.m_strEndpoint),
                       strKey = "opds-atom-v1\n" + strInitial;
            if (m_pCache &&
                (ePolicy == CachePolicy::PreferCache || ePolicy == CachePolicy::CacheOnly))
            {
                if (auto oBytes = m_pCache->read(strKey, m_oOptions.m_oCacheMaxAge))
                {
                    auto nSplit = oBytes->find('\n');
                    if (nSplit != std::string::npos)
                    {
                        try
                        {
                            auto strFinalUrl = sameOrigin(oBytes->substr(0, nSplit), strInitial);
                            return oParse(oBytes->substr(nSplit + 1), strFinalUrl);
                        }
                        catch (const Error& oE)
                        {
                            if (oE.code() != ErrorCode::Parse &&
                                oE.code() != ErrorCode::InvalidArgument)
                                throw;
                        }
                    }
                }
            }
            if (ePolicy == CachePolicy::CacheOnly)
                throw Error(ErrorCode::CacheMiss, "No valid cached catalog document");
            auto strCurrent = strInitial;
            auto oTransfer = m_oOptions.m_oTransfer;
            oTransfer.m_bFollowRedirects = false;
            for (int nRedirects = 0; nRedirects <= 5; ++nRedirects)
            {
                auto oResponse = m_pTransport->perform(
                    {strCurrent,
                     "GET",
                     {},
                     {{"Accept",
                       "application/atom+xml,application/opensearchdescription+xml;q=0.9"}}},
                    oTransfer);
                if (oResponse.m_nStatus >= 300 && oResponse.m_nStatus < 400)
                {
                    auto strNext = header(oResponse, "location");
                    if (strNext.empty())
                        throw Error(ErrorCode::Http, "Catalog redirect has no location",
                                    oResponse.m_nStatus);
                    strCurrent = sameOrigin(strNext, strCurrent);
                    continue;
                }
                if (oResponse.m_nStatus != 200)
                    throw Error(ErrorCode::Http, "Catalog request failed", oResponse.m_nStatus);
                if (!oResponse.m_strEffectiveUrl.empty())
                    strCurrent = sameOrigin(oResponse.m_strEffectiveUrl, strCurrent);
                auto oResult = oParse(oResponse.m_strBody, strCurrent);
                if (m_pCache && ePolicy != CachePolicy::NetworkOnly)
                    m_pCache->write(strKey, strCurrent + '\n' + oResponse.m_strBody);
                return oResult;
            }
            throw Error(ErrorCode::Http, "Too many catalog redirects");
        }
    };
    CatalogClient::CatalogClient(CatalogOptions oOptions, std::shared_ptr<HttpTransport> pTransport)
        : m_pImpl(new Impl)
    {
        m_pImpl->m_strSourceOrigin = origin(oOptions.m_strEndpoint);
        if (oOptions.m_oCacheMaxAge.count() < 0)
            throw Error(ErrorCode::InvalidArgument, "Negative catalog cache lifetime");
        m_pImpl->m_oOptions = std::move(oOptions);
        m_pImpl->m_pTransport = pTransport ? std::move(pTransport) : makeCurlTransport();
        if (!m_pImpl->m_oOptions.m_oCacheDirectory.empty())
            m_pImpl->m_pCache.reset(new FileCache(m_pImpl->m_oOptions.m_oCacheDirectory));
    }
    CatalogClient::~CatalogClient() = default;
    CatalogClient::CatalogClient(CatalogClient&&) noexcept = default;
    CatalogClient& CatalogClient::operator=(CatalogClient&&) noexcept = default;
    CatalogPage CatalogClient::browse(const std::string& strUrl, CachePolicy ePolicy)
    {
        return m_pImpl->fetch(strUrl.empty() ? m_pImpl->m_oOptions.m_strEndpoint : strUrl, ePolicy,
                              [this](const auto& oXml, const auto& oBase)
                              {
                                  return m_pImpl->page(oXml, oBase);
                              });
    }
    CatalogPage CatalogClient::list(Sort eSort, CachePolicy ePolicy)
    {
        static const char* vKeys[] = {
            "allvisit", "allvote",  "monthvisit", "monthvote", "weekvisit", "weekvote", "dayvisit",
            "dayvote",  "postdate", "lastupdate", "goodnum",   "size",      "fullflag"};
        auto nIndex = static_cast<unsigned>(eSort);
        if (nIndex >= sizeof(vKeys) / sizeof(vKeys[0]))
            throw Error(ErrorCode::InvalidArgument, "Invalid catalog sort");
        auto oRoot = browse({}, ePolicy);
        for (const auto& oCollection : oRoot.m_vCollections)
        {
            auto strUrl = oCollection.m_strUrl;
            if (auto nQuery = strUrl.find('?'); nQuery != std::string::npos)
                strUrl.resize(nQuery);
            auto nSlash = strUrl.rfind('/');
            auto strName = lower(nSlash == std::string::npos ? strUrl : strUrl.substr(nSlash + 1));
            if (strName == std::string(vKeys[nIndex]) + ".xml")
                return browse(oCollection.m_strUrl, ePolicy);
        }
        throw Error(ErrorCode::Unsupported, "Selected sort is not advertised by catalog");
    }
    CatalogPage CatalogClient::search(const std::string& strQuery, CachePolicy ePolicy)
    {
        if (strQuery.empty())
            throw Error(ErrorCode::InvalidArgument, "Empty catalog search");
        auto oRoot = browse({}, ePolicy);
        if (oRoot.m_strSearchDescriptionUrl.empty())
            throw Error(ErrorCode::Unsupported, "Catalog does not advertise search");
        auto oDescriptor = m_pImpl->fetch(
            oRoot.m_strSearchDescriptionUrl, ePolicy,
            [](const auto& oXml, const auto& oBase)
            {
                tinyxml2::XMLDocument oDoc;
                auto oRoot = parse(oDoc, oXml, "OpenSearchDescription", pOpenSearch);
                for (auto oE = oRoot->FirstChildElement(); oE; oE = oE->NextSiblingElement())
                    if (named(oE, "Url", pOpenSearch) && atomType(attribute(oE, "type")))
                    {
                        if (!attribute(oE, "method").empty() && attribute(oE, "method") != "GET")
                            continue;
                        auto oValue = attribute(oE, "template");
                        if (oValue.find("{searchTerms}") != std::string::npos)
                            return std::make_pair(oValue, oBase);
                    }
                throw Error(ErrorCode::Unsupported, "Unsupported OpenSearch template");
            });
        auto strSearchTemplate = oDescriptor.first;
        const auto strReplacement = formEncode({{"q", strQuery}}).substr(2);
        const std::string strPlaceholder = "{searchTerms}";
        for (auto nAt = strSearchTemplate.find(strPlaceholder); nAt != std::string::npos;
             nAt = strSearchTemplate.find(strPlaceholder))
            strSearchTemplate.replace(nAt, strPlaceholder.size(), strReplacement);
        if (strSearchTemplate.find_first_of("{}") != std::string::npos)
            throw Error(ErrorCode::Unsupported, "OpenSearch has unsupported parameters");
        return browse(m_pImpl->sameOrigin(strSearchTemplate, oDescriptor.second), ePolicy);
    }
} // namespace Wenku8
