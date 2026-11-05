// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Api.h"
#include "tinyxml2.h"
#include <charconv>
#include <limits>

namespace Wenku8
{
    namespace
    {
        void positive(int nValue)
        {
            if (nValue <= 0)
                throw Error(ErrorCode::InvalidArgument, "ID and page must be positive");
        }
        std::string language(Language eLang)
        {
            if (eLang != Language::Simplified && eLang != Language::Traditional)
                throw Error(ErrorCode::InvalidArgument, "Invalid language");
            return eLang == Language::Simplified ? "0" : "1";
        }
        std::string base64(const std::string& strIn)
        {
            static constexpr char vAlphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string strOut;
            for (std::size_t nI = 0; nI < strIn.size(); nI += 3)
            {
                unsigned nN = static_cast<unsigned char>(strIn[nI]) << 16;
                if (nI + 1 < strIn.size())
                    nN |= static_cast<unsigned char>(strIn[nI + 1]) << 8;
                if (nI + 2 < strIn.size())
                    nN |= static_cast<unsigned char>(strIn[nI + 2]);
                strOut += vAlphabet[(nN >> 18) & 63];
                strOut += vAlphabet[(nN >> 12) & 63];
                strOut += nI + 1 < strIn.size() ? vAlphabet[(nN >> 6) & 63] : '=';
                strOut += nI + 2 < strIn.size() ? vAlphabet[nN & 63] : '=';
            }
            return strOut;
        }
        std::string text(const tinyxml2::XMLElement* pE)
        {
            std::string strResult;
            for (auto pN = pE->FirstChild(); pN; pN = pN->NextSibling())
                if (auto pT = pN->ToText())
                    strResult += pT->Value();
            return strResult;
        }
        std::string attr(const tinyxml2::XMLElement* pE, const char* pName)
        {
            const char* pValue = pE->Attribute(pName);
            return pValue ? pValue : "";
        }
        std::uint64_t number(const std::string& strValue)
        {
            std::uint64_t nResult = 0;
            auto oR = std::from_chars(strValue.data(), strValue.data() + strValue.size(), nResult);
            if (strValue.empty() || oR.ec != std::errc{} ||
                oR.ptr != strValue.data() + strValue.size())
                throw Error(ErrorCode::Parse, "Invalid numeric XML field");
            return nResult;
        }
        int integer(const std::string& strValue, bool bNonzero = true)
        {
            const auto nN = number(strValue);
            if ((bNonzero && nN == 0) ||
                nN > static_cast<unsigned>(std::numeric_limits<int>::max()))
                throw Error(ErrorCode::Parse, "XML ID or page out of range");
            return static_cast<int>(nN);
        }
        const tinyxml2::XMLElement* root(tinyxml2::XMLDocument& oDoc, const std::string& strXml,
                                         const char* pName)
        {
            if (strXml.find('\0') != std::string::npos ||
                oDoc.Parse(strXml.data(), strXml.size()) != tinyxml2::XML_SUCCESS ||
                !oDoc.RootElement() || std::string(oDoc.RootElement()->Name()) != pName ||
                oDoc.RootElement()->NextSiblingElement())
                throw Error(ErrorCode::Parse, "Unexpected or malformed XML response");
            // This protocol has no DTD and never resolves external entities.
            for (auto pNode = oDoc.FirstChild(); pNode; pNode = pNode->NextSibling())
                if (pNode->ToUnknown())
                    throw Error(ErrorCode::Parse, "Unsupported XML declaration");
            return oDoc.RootElement();
        }
        Novel parseNovel(const tinyxml2::XMLElement* pE, int nId)
        {
            Novel oN;
            oN.m_nId = nId;
            for (auto pD = pE->FirstChildElement("data"); pD; pD = pD->NextSiblingElement("data"))
            {
                const auto strName = attr(pD, "name");
                const auto strValue = attr(pD, "value");
                if (strName == "Title")
                {
                    oN.m_strTitle = text(pD);
                    if (pD->Attribute("aid"))
                        oN.m_nId = integer(attr(pD, "aid"));
                }
                else if (strName == "Author")
                    oN.m_strAuthor = strValue;
                else if (strName == "BookStatus")
                    oN.m_strStatus = strValue;
                else if (strName == "LastUpdate")
                    oN.m_strLastUpdate = strValue;
                else if (strName == "IntroPreview")
                    oN.m_strIntro = text(pD);
                else if (strName == "PressId")
                    oN.m_strPublisher = strValue;
                else if (strName == "TotalHitsCount")
                    oN.m_nHits = number(strValue);
                else if (strName == "PushCount")
                    oN.m_nVotes = number(strValue);
                else if (strName == "FavCount")
                    oN.m_nFavorites = number(strValue);
                else if (strName == "BookLength")
                    oN.m_nLength = number(strValue);
                else if (strName == "LatestSection")
                {
                    oN.m_strLatestChapter = text(pD);
                    oN.m_nLatestChapterId = integer(attr(pD, "cid"));
                }
            }
            return oN;
        }
    } // namespace

    HttpRequest LegacyApi::request(const std::string& strEndpoint, const Fields& vParameters,
                                   const std::string& strAppVersion)
    {
        // The inner parameter values and the outer base64 form each need encoding.
        Fields vEnvelope{{"request", base64(formEncode(vParameters))}};
        if (!strAppVersion.empty())
        {
            vEnvelope.insert(vEnvelope.begin(), {"appver", strAppVersion});
            auto oNow = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());
            vEnvelope.emplace_back("timetoken", std::to_string(oNow.count()));
        }
        return {strEndpoint,
                "POST",
                formEncode(vEnvelope),
                {{"Content-Type", "application/x-www-form-urlencoded"}}};
    }
    Fields LegacyApi::book(const std::string& strOperation, int nId, Language eLang)
    {
        positive(nId);
        if (strOperation != "cover" && strOperation != "info" && strOperation != "intro" &&
            strOperation != "meta" && strOperation != "list" && strOperation != "text")
            throw Error(ErrorCode::InvalidArgument, "Unknown book operation");
        Fields vResult{{"action", "book"}, {"do", strOperation}, {"aid", std::to_string(nId)}};
        if (strOperation != "cover")
            vResult.emplace_back("t", language(eLang));
        return vResult;
    }
    Fields LegacyApi::list(Sort eSort, int nPage, Language eLang)
    {
        positive(nPage);
        static const char* vNames[] = {
            "allvisit", "allvote",  "monthvisit", "monthvote", "weekvisit", "weekvote", "dayvisit",
            "dayvote",  "postdate", "lastupdate", "goodnum",   "size",      "fullflag"};
        auto nI = static_cast<unsigned>(eSort);
        if (nI >= sizeof(vNames) / sizeof(vNames[0]))
            throw Error(ErrorCode::InvalidArgument, "Invalid sort");
        return {{"action", "novellist"},
                {"sort", vNames[nI]},
                {"page", std::to_string(nPage)},
                {"t", language(eLang)}};
    }
    Fields LegacyApi::search(const std::string& strQuery, SearchType eType, Language eLang)
    {
        if (strQuery.empty() || (eType != SearchType::Title && eType != SearchType::Author))
            throw Error(ErrorCode::InvalidArgument, "Invalid search");
        return {{"action", "search"},
                {"searchtype", eType == SearchType::Title ? "articlename" : "author"},
                {"searchkey", strQuery},
                {"t", language(eLang)}};
    }
    Fields LegacyApi::library(int nId, int nPage, Language eLang)
    {
        positive(nId);
        positive(nPage);
        return {{"action", "novellist"},
                {"sort", std::to_string(nId)},
                {"page", std::to_string(nPage)},
                {"t", language(eLang)}};
    }
    namespace Parser
    {
        void operationSucceeded(const std::string& strResponse)
        {
            // v1.9 LightUserSession requires an integer and treats only code 1 as success.
            int nCode = 0;
            auto oR =
                std::from_chars(strResponse.data(), strResponse.data() + strResponse.size(), nCode);
            if (strResponse.empty() || oR.ec != std::errc{} ||
                oR.ptr != strResponse.data() + strResponse.size())
                throw Error(ErrorCode::Parse, "Expected integer operation result");
            if (nCode != 1)
                throw Error(ErrorCode::Server, "Server rejected operation", 0, nCode);
        }
        UserProfile userInfo(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto pR = root(oDoc, strXml, "metadata");
            UserProfile oResult;
            for (auto pItem = pR->FirstChildElement("item"); pItem;
                 pItem = pItem->NextSiblingElement("item"))
            {
                auto strName = attr(pItem, "name");
                auto strValue = text(pItem);
                if (strName == "uname")
                    oResult.m_strUsername = strValue;
                else if (strName == "nickname")
                    oResult.m_strNickname = strValue;
                else if (strName == "rank")
                    oResult.m_strRank = strValue;
                else if (strName == "uid")
                    oResult.m_nId = integer(strValue);
                else if (strName == "score")
                    oResult.m_nScore = number(strValue);
                else if (strName == "experience")
                    oResult.m_nExperience = number(strValue);
            }
            if (oResult.m_strUsername.empty())
                throw Error(ErrorCode::Parse, "Missing account username");
            return oResult;
        }
        std::vector<Novel> bookshelf(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto pR = root(oDoc, strXml, "metadata");
            std::vector<Novel> vResult;
            for (auto pItem = pR->FirstChildElement(); pItem; pItem = pItem->NextSiblingElement())
            {
                if (std::string(pItem->Name()) != "book")
                    throw Error(ErrorCode::Parse, "Unexpected bookshelf element");
                Novel oN;
                oN.m_nId = integer(attr(pItem, "aid"));
                oN.m_strLastUpdate = attr(pItem, "date");
                if (auto pName = pItem->FirstChildElement("name"))
                    oN.m_strTitle = text(pName);
                if (auto pChapter = pItem->FirstChildElement("chapter"))
                {
                    oN.m_nLatestChapterId = integer(attr(pChapter, "cid"));
                    oN.m_strLatestChapter = text(pChapter);
                }
                vResult.push_back(std::move(oN));
            }
            return vResult;
        }
        NovelPage novelPage(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto pR = root(oDoc, strXml, "result");
            NovelPage oResult;
            if (auto pP = pR->FirstChildElement("page"))
                oResult.m_nPages = integer(attr(pP, "num"), false);
            for (auto pItem = pR->FirstChildElement("item"); pItem;
                 pItem = pItem->NextSiblingElement("item"))
                oResult.m_vNovels.push_back(parseNovel(pItem, integer(attr(pItem, "aid"))));
            for (auto pE = pR->FirstChildElement(); pE; pE = pE->NextSiblingElement())
                if (std::string(pE->Name()) != "page" && std::string(pE->Name()) != "item")
                    throw Error(ErrorCode::Parse, "Unexpected list element");
            return oResult;
        }
        Novel novel(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto oN = parseNovel(root(oDoc, strXml, "metadata"), 0);
            if (oN.m_nId <= 0 || oN.m_strTitle.empty())
                throw Error(ErrorCode::Parse, "Missing novel identity");
            return oN;
        }
        std::vector<Volume> index(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto pR = root(oDoc, strXml, "package");
            std::vector<Volume> vResult;
            for (auto pV = pR->FirstChildElement("volume"); pV;
                 pV = pV->NextSiblingElement("volume"))
            {
                Volume oVolume;
                oVolume.m_nId = integer(attr(pV, "vid"));
                oVolume.m_strTitle = text(pV);
                for (auto pC = pV->FirstChildElement("chapter"); pC;
                     pC = pC->NextSiblingElement("chapter"))
                    oVolume.m_vChapters.push_back({integer(attr(pC, "cid")), text(pC)});
                vResult.push_back(std::move(oVolume));
            }
            if (vResult.empty())
                throw Error(ErrorCode::Parse, "Empty chapter index");
            return vResult;
        }
        std::vector<Library> libraries(const std::string& strXml)
        {
            tinyxml2::XMLDocument oDoc;
            auto pR = root(oDoc, strXml, "metadata");
            std::vector<Library> vResult;
            for (auto pE = pR->FirstChildElement("item"); pE; pE = pE->NextSiblingElement("item"))
                vResult.push_back({integer(attr(pE, "sort")), text(pE)});
            if (vResult.empty())
                throw Error(ErrorCode::Parse, "Empty library list");
            return vResult;
        }
    } // namespace Parser

    namespace
    {
        class LegacyAuthentication final : public AuthenticationProtocol
        {
            std::string m_strEndpoint, m_strVersion;

          public:
            LegacyAuthentication(std::string strEndpoint, std::string strVersion)
                : m_strEndpoint(std::move(strEndpoint)), m_strVersion(std::move(strVersion))
            {
            }
            HttpRequest loginRequest(const std::string& strUsername,
                                     const std::string& strPassword) const override
            {
                return LegacyApi::request(
                    m_strEndpoint,
                    {{"action", "login"}, {"username", strUsername}, {"password", strPassword}},
                    m_strVersion);
            }
            User parseLogin(const HttpResponse& oResponse,
                            const std::string& strName) const override
            {
                try
                {
                    Parser::operationSucceeded(oResponse.m_strBody);
                }
                catch (const Error& oE)
                {
                    if (oE.code() == ErrorCode::Server)
                        throw Error(ErrorCode::Authentication, "Server rejected login", 0,
                                    oE.apiCode());
                    throw;
                }
                return {0, strName};
            }
            HttpRequest logoutRequest() const override
            {
                return LegacyApi::request(m_strEndpoint, {{"action", "logout"}}, m_strVersion);
            }
            void validateLogout(const HttpResponse& oResponse) const override
            {
                Parser::operationSucceeded(oResponse.m_strBody);
            }
        };
    } // namespace
    std::shared_ptr<AuthenticationProtocol> makeLegacyAuthentication(const std::string& strEndpoint,
                                                                     const std::string& strVersion)
    {
        return std::make_shared<LegacyAuthentication>(strEndpoint, strVersion);
    }
} // namespace Wenku8
