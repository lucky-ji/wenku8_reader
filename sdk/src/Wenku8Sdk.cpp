// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Api.h"
#include "Storage.h"
#include "WebBackend.h"
#include "TextPackage.h"

namespace Wenku8
{
    namespace
    {
        void httpOk(const HttpResponse& oResponse)
        {
            if (oResponse.m_nStatus < 200 || oResponse.m_nStatus >= 300)
                throw Error(ErrorCode::Http, "HTTP request failed", oResponse.m_nStatus);
        }
        std::string nonempty(const std::string& strBytes)
        {
            if (strBytes.empty())
                throw Error(ErrorCode::Parse, "Empty API response");
            // Detect common HTML error/challenge pages returned with HTTP 200.
            auto nStart = strBytes.find_first_not_of(" \r\n\t");
            if (nStart != std::string::npos && strBytes[nStart] == '<' &&
                (strBytes.compare(nStart, 5, "<html") == 0 ||
                 strBytes.compare(nStart, 9, "<!DOCTYPE") == 0 ||
                 strBytes.compare(nStart, 9, "<!doctype") == 0))
                throw Error(ErrorCode::Parse, "HTML page instead of API content");
            return strBytes;
        }
        std::string image(const std::string& strBytes)
        {
            const bool bJpeg = strBytes.size() >= 3 &&
                               static_cast<unsigned char>(strBytes[0]) == 0xff &&
                               static_cast<unsigned char>(strBytes[1]) == 0xd8 &&
                               static_cast<unsigned char>(strBytes[2]) == 0xff;
            const bool bPng =
                strBytes.size() >= 8 && strBytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
            const bool bGif =
                strBytes.compare(0, 6, "GIF87a") == 0 || strBytes.compare(0, 6, "GIF89a") == 0;
            const bool bWebp = strBytes.size() >= 12 && strBytes.compare(0, 4, "RIFF") == 0 &&
                               strBytes.compare(8, 4, "WEBP") == 0;
            if (!bJpeg && !bPng && !bGif && !bWebp)
                throw Error(ErrorCode::Parse, "Unexpected cover format");
            return strBytes;
        }
    } // namespace
    struct Client::Impl
    {
        ClientOptions m_oOptions;
        std::shared_ptr<HttpTransport> m_pTransport;
        std::unique_ptr<FileCache> m_pCache;
        std::optional<User> m_oUser;
        std::unique_ptr<Detail::WebBackend> m_pWeb;
        HttpResponse accountRequest(const Fields& vFields)
        {
            if (!m_oUser)
                throw Error(ErrorCode::Authentication, "Login required");
            auto oResponse =
                m_pTransport->perform(LegacyApi::request(m_oOptions.m_strEndpoint, vFields,
                                                         m_oOptions.m_strLegacyAppVersion),
                                      m_oOptions.m_oTransfer);
            httpOk(oResponse);
            return oResponse;
        }
        void mutate(const Fields& vFields)
        {
            Parser::operationSucceeded(accountRequest(vFields).m_strBody);
        }
        template <class Parse>
        auto fetch(const Fields& vParameters, CachePolicy ePolicy, Parse oParse)
            -> decltype(oParse(std::string{}))
        {
            if (ePolicy != CachePolicy::NetworkOnly && ePolicy != CachePolicy::PreferCache &&
                ePolicy != CachePolicy::CacheOnly && ePolicy != CachePolicy::Refresh)
                throw Error(ErrorCode::InvalidArgument, "Invalid cache policy");
            auto oRequest = LegacyApi::request(m_oOptions.m_strEndpoint, vParameters,
                                               m_oOptions.m_strLegacyAppVersion);
            // The v1.x timestamp is transfer metadata, not cache identity.
            const auto strKey = "legacy-v1\n" + oRequest.m_strUrl + "\n" +
                                formEncode({{"version", m_oOptions.m_strLegacyAppVersion}}) + "\n" +
                                formEncode(vParameters);
            if (m_pCache &&
                (ePolicy == CachePolicy::PreferCache || ePolicy == CachePolicy::CacheOnly))
            {
                if (auto oBytes = m_pCache->read(strKey, m_oOptions.m_oCacheMaxAge))
                {
                    try
                    {
                        return oParse(*oBytes);
                    }
                    catch (const Error& oE)
                    {
                        if (oE.code() != ErrorCode::Parse)
                            throw;
                        if (ePolicy == CachePolicy::CacheOnly)
                            throw Error(ErrorCode::CacheMiss, "Invalid cached document");
                    }
                }
            }
            if (ePolicy == CachePolicy::CacheOnly)
                throw Error(ErrorCode::CacheMiss, "No valid cached response");
            auto oResponse = m_pTransport->perform(oRequest, m_oOptions.m_oTransfer);
            httpOk(oResponse);
            auto oResult = oParse(oResponse.m_strBody); // Validate before publishing to cache.
            if (m_pCache && ePolicy != CachePolicy::NetworkOnly)
                m_pCache->write(strKey, oResponse.m_strBody);
            return oResult;
        }
    };
    Client::Client(ClientOptions oOptions, std::shared_ptr<HttpTransport> pTransport)
        : m_pImpl(new Impl)
    {
        if ((oOptions.m_strEndpoint.rfind("https://", 0) != 0 &&
             oOptions.m_strEndpoint.rfind("http://", 0) != 0) ||
            oOptions.m_strEndpoint.find('\0') != std::string::npos ||
            oOptions.m_oCacheMaxAge.count() < 0 ||
            (oOptions.m_eLanguage != Language::Simplified &&
             oOptions.m_eLanguage != Language::Traditional) ||
            (oOptions.m_eProtocol != Protocol::LegacyApi &&
             oOptions.m_eProtocol != Protocol::Website))
            throw Error(ErrorCode::InvalidArgument, "Invalid client options");
        m_pImpl->m_oOptions = std::move(oOptions);
        m_pImpl->m_pTransport = pTransport ? std::move(pTransport) : makeCurlTransport();
        if (!m_pImpl->m_oOptions.m_oCacheDirectory.empty())
            m_pImpl->m_pCache.reset(new FileCache(m_pImpl->m_oOptions.m_oCacheDirectory));
        if (m_pImpl->m_oOptions.m_eProtocol == Protocol::Website)
            m_pImpl->m_pWeb.reset(
                new Detail::WebBackend(m_pImpl->m_oOptions, m_pImpl->m_pTransport));
    }
    Client::~Client() = default;
    Client::Client(Client&&) noexcept = default;
    Client& Client::operator=(Client&&) noexcept = default;
    NovelPage Client::list(Sort eSort, int nPage, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->list(eSort, nPage, ePolicy);
        return m_pImpl->fetch(LegacyApi::list(eSort, nPage, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, Parser::novelPage);
    }
    NovelPage Client::search(const std::string& strQuery, SearchType eType, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->search(strQuery, eType, ePolicy);
        return m_pImpl->fetch(LegacyApi::search(strQuery, eType, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, Parser::novelPage);
    }
    Novel Client::novel(int nId, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->novel(nId, ePolicy);
        return m_pImpl->fetch(LegacyApi::book("meta", nId, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy,
                              [nId](const std::string& strXml)
                              {
                                  auto oResult = Parser::novel(strXml);
                                  if (oResult.m_nId != nId)
                                      throw Error(ErrorCode::Parse, "Novel ID mismatch");
                                  return oResult;
                              });
    }
    std::string Client::introduction(int nId, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->introduction(nId, ePolicy);
        return m_pImpl->fetch(LegacyApi::book("intro", nId, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, nonempty);
    }
    std::vector<Volume> Client::index(int nId, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->index(nId, ePolicy);
        return m_pImpl->fetch(LegacyApi::book("list", nId, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, Parser::index);
    }
    std::string Client::chapter(int nId, int nChapterId, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->chapter(nId, nChapterId, ePolicy);
        if (nChapterId <= 0)
            throw Error(ErrorCode::InvalidArgument, "Invalid chapter ID");
        auto vFields = LegacyApi::book("text", nId, m_pImpl->m_oOptions.m_eLanguage);
        // Preserve the historical request ordering aid,cid,t.
        vFields.insert(vFields.end() - 1, {"cid", std::to_string(nChapterId)});
        return m_pImpl->fetch(vFields, ePolicy, nonempty);
    }
    std::string Client::cover(int nId, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->cover(nId, ePolicy);
        return m_pImpl->fetch(LegacyApi::book("cover", nId, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, image);
    }
    std::vector<Library> Client::libraries(CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->libraries(ePolicy);
        return m_pImpl->fetch({{"action", "xml"}, {"item", "sort"}, {"t", "0"}}, ePolicy,
                              Parser::libraries);
    }
    NovelPage Client::library(int nId, int nPage, CachePolicy ePolicy)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->library(nId, nPage, ePolicy);
        return m_pImpl->fetch(LegacyApi::library(nId, nPage, m_pImpl->m_oOptions.m_eLanguage),
                              ePolicy, Parser::novelPage);
    }
    void Client::downloadNovel(int nId, CachePolicy ePolicy)
    {
        if (!m_pImpl->m_pCache || ePolicy == CachePolicy::NetworkOnly)
            throw Error(ErrorCode::InvalidArgument, "Novel download requires cache storage");
        novel(nId, ePolicy);
        introduction(nId, ePolicy);
        auto vVolumes = index(nId, ePolicy);
        for (const auto& oVolume : vVolumes)
            for (const auto& oItem : oVolume.m_vChapters)
                chapter(nId, oItem.m_nId, ePolicy);
    }
    User Client::login(const std::string& strName, const std::string& strPassword,
                       const std::string& strCaptcha)
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->login(strName, strPassword, strCaptcha);
        if (!strCaptcha.empty())
            throw Error(ErrorCode::Unsupported, "Captcha belongs to the website protocol");
        if (!m_pImpl->m_oOptions.m_pAuthentication)
            throw Error(ErrorCode::Unsupported, "Verified authentication protocol required");
        if (strName.empty() || strPassword.empty())
            throw Error(ErrorCode::InvalidArgument, "Empty credentials");
        m_pImpl->m_oUser.reset();
        m_pImpl->m_pTransport->clearCookies();
        try
        {
            auto oTransfer = m_pImpl->m_oOptions.m_oTransfer;
            // Do not forward a credential-bearing POST to a redirect destination.
            oTransfer.m_bFollowRedirects = false;
            auto oResponse = m_pImpl->m_pTransport->perform(
                m_pImpl->m_oOptions.m_pAuthentication->loginRequest(strName, strPassword),
                oTransfer);
            httpOk(oResponse);
            auto oUser = m_pImpl->m_oOptions.m_pAuthentication->parseLogin(oResponse, strName);
            if (oUser.m_nId < 0 || oUser.m_strName.empty())
                throw Error(ErrorCode::Authentication, "Invalid login identity");
            m_pImpl->m_oUser = oUser;
            return oUser;
        }
        catch (...)
        {
            m_pImpl->m_pTransport->clearCookies();
            throw;
        }
    }
    void Client::logout()
    {
        if (m_pImpl->m_pWeb)
        {
            m_pImpl->m_pWeb->logout();
            return;
        }
        // Clear local session even if the server cannot be reached.
        auto pProtocol = m_pImpl->m_oOptions.m_pAuthentication;
        m_pImpl->m_oUser.reset();
        if (!pProtocol)
        {
            m_pImpl->m_pTransport->clearCookies();
            return;
        }
        try
        {
            auto oResponse = m_pImpl->m_pTransport->perform(pProtocol->logoutRequest(),
                                                            m_pImpl->m_oOptions.m_oTransfer);
            httpOk(oResponse);
            pProtocol->validateLogout(oResponse);
        }
        catch (...)
        {
            m_pImpl->m_pTransport->clearCookies();
            throw;
        }
        m_pImpl->m_pTransport->clearCookies();
    }
    std::optional<User> Client::user() const
    {
        return m_pImpl->m_pWeb ? m_pImpl->m_pWeb->user() : m_pImpl->m_oUser;
    }
    LoginRequirements Client::prepareLogin()
    {
        if (!m_pImpl->m_pWeb)
            throw Error(ErrorCode::Unsupported, "Login preparation belongs to website protocol");
        return m_pImpl->m_pWeb->prepareLogin();
    }
    std::string Client::captcha()
    {
        if (!m_pImpl->m_pWeb)
            throw Error(ErrorCode::Unsupported, "Captcha belongs to website protocol");
        return m_pImpl->m_pWeb->captcha();
    }
    UserProfile Client::restoreSession(const std::string& strCookies)
    {
        if (!m_pImpl->m_pWeb)
            throw Error(ErrorCode::Unsupported, "Browser session belongs to website protocol");
        return m_pImpl->m_pWeb->restoreSession(strCookies);
    }
    UserProfile Client::userInfo()
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->userInfo();
        auto oResult =
            Parser::userInfo(m_pImpl->accountRequest({{"action", "userinfo"}}).m_strBody);
        m_pImpl->m_oUser = User{oResult.m_nId, oResult.m_strUsername};
        return oResult;
    }
    std::vector<Novel> Client::bookshelf()
    {
        if (m_pImpl->m_pWeb)
            return m_pImpl->m_pWeb->bookshelf();
        return Parser::bookshelf(
            m_pImpl
                ->accountRequest(
                    {{"action", "bookcase"},
                     {"t", m_pImpl->m_oOptions.m_eLanguage == Language::Simplified ? "0" : "1"}})
                .m_strBody);
    }
    void Client::addToBookshelf(int nId)
    {
        if (m_pImpl->m_pWeb)
        {
            m_pImpl->m_pWeb->addToBookshelf(nId);
            return;
        }
        if (nId <= 0)
            throw Error(ErrorCode::InvalidArgument, "Invalid novel ID");
        m_pImpl->mutate({{"action", "bookcase"}, {"do", "add"}, {"aid", std::to_string(nId)}});
    }
    void Client::removeFromBookshelf(int nId)
    {
        if (m_pImpl->m_pWeb)
            throw Error(ErrorCode::Unsupported, "Website bookshelf writes not yet implemented");
        if (nId <= 0)
            throw Error(ErrorCode::InvalidArgument, "Invalid novel ID");
        m_pImpl->mutate({{"action", "bookcase"}, {"do", "del"}, {"aid", std::to_string(nId)}});
    }
    void Client::vote(int nId)
    {
        if (m_pImpl->m_pWeb)
            throw Error(ErrorCode::Unsupported, "Website voting not yet implemented");
        if (nId <= 0)
            throw Error(ErrorCode::InvalidArgument, "Invalid novel ID");
        m_pImpl->mutate({{"action", "book"}, {"do", "vote"}, {"aid", std::to_string(nId)}});
    }
    void Client::download(const std::string& strUrl, const std::filesystem::path& oDestination)
    {
        auto oResponse = m_pImpl->m_pTransport->perform({strUrl, "GET", {}, {}},
                                                        m_pImpl->m_oOptions.m_oTransfer);
        httpOk(oResponse);
        Detail::atomicWrite(oDestination, oResponse.m_strBody);
    }
    void Client::downloadTextPackage(int nId, const std::filesystem::path& oDestination,
                                     CachePolicy ePolicy)
    {
        Detail::downloadTextPackage(nId, oDestination, ePolicy, *m_pImpl->m_pTransport,
                                    m_pImpl->m_oOptions.m_oTransfer, m_pImpl->m_pCache.get(),
                                    m_pImpl->m_oOptions.m_oCacheMaxAge);
    }
} // namespace Wenku8
