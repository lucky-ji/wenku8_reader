// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) && !defined(WENKU8_STATIC)
#ifdef WENKU8SDK_EXPORTS
#define WENKU8_API __declspec(dllexport)
#else
#define WENKU8_API __declspec(dllimport)
#endif
#else
#define WENKU8_API
#endif

namespace Wenku8
{
    enum class ErrorCode
    {
        InvalidArgument,
        Network,
        Http,
        Cancelled,
        ResponseTooLarge,
        Parse,
        Io,
        CacheMiss,
        Unsupported,
        Authentication,
        Server,
        BrowserVerificationRequired,
        CaptchaRequired,
        SiteAccessDenied
    };
    class WENKU8_API Error : public std::runtime_error
    {
      public:
        Error(ErrorCode eCode, const std::string& strMessage, long nStatus = 0, int nApiCode = 0,
              int nTransportCode = 0);
        ErrorCode code() const noexcept
        {
            return m_eCode;
        }
        long httpStatus() const noexcept
        {
            return m_nStatus;
        }
        int apiCode() const noexcept
        {
            return m_nApiCode;
        }
        int transportCode() const noexcept
        {
            return m_nTransportCode;
        } // libcurl CURLcode; 0 for non-curl errors.
      private:
        ErrorCode m_eCode;
        long m_nStatus;
        int m_nApiCode;
        int m_nTransportCode;
    };
    using Fields = std::vector<std::pair<std::string, std::string>>;
    struct HttpRequest
    {
        std::string m_strUrl;
        std::string m_strMethod = "GET";
        std::string m_strBody;
        Fields m_vHeaders;
        bool m_bAllowRetries = true; // Set false for state-changing GETs.
    };
    struct HttpResponse
    {
        long m_nStatus = 0;
        std::string m_strBody; // Binary-safe; legacy API text uses UTF-8.
        std::string m_strEffectiveUrl;
        Fields m_vHeaders; // Final response headers; may contain sensitive Set-Cookie values.
    };
    struct TransferOptions
    {
        std::chrono::milliseconds m_oConnectTimeout{10000};
        std::chrono::milliseconds m_oTimeout{60000};
        std::size_t m_nMaxResponseBytes = 32 * 1024 * 1024;
        bool m_bFollowRedirects = true;
        // Return false to cancel. Must not re-enter Client.
        std::function<bool(std::uint64_t, std::uint64_t)> m_fnProgress;
    };
    class WENKU8_API HttpTransport
    {
      public:
        virtual ~HttpTransport() = default;
        virtual HttpResponse perform(const HttpRequest&, const TransferOptions&) = 0;
        // True only when GET redirects with followRedirects=true are restricted to
        // the original origin by the transport, before fetching another origin.
        virtual bool supportsSameOriginGetRedirects() const noexcept
        {
            return false;
        }
        virtual void clearCookies() = 0;
        virtual void importCookies(const std::string&, const std::string&)
        {
            throw Error(ErrorCode::Unsupported, "Cookie import unavailable");
        }
    };
    // Owns a cookie session. TLS peer/host verification stays enabled.
    WENKU8_API std::shared_ptr<HttpTransport> makeCurlTransport(
        const std::string& strCaBundle = {});
    struct RetryOptions
    {
        unsigned m_nMaxAttempts = 3; // Includes initial attempt; allowed range 1..10.
        std::chrono::milliseconds m_oDelay{250};
        // Opt-in response retries; permitted: 404 (assets), 408, 425, 429, 500, 502, 503, 504.
        std::vector<long> m_vHttpStatuses;
        bool m_bRetryBrowserNetworkErrors = false; // Network errors with native code 0.
        // Explicitly retry read-only GET refusals (403 or recognised challenge
        // responses/errors), at most three attempts. No verification bypass,
        // cookie reset or 401 retry is performed.
        bool m_bRetryRefusedResponses = false;
        bool m_bExponentialBackoff = false;
        std::chrono::milliseconds m_oAttemptTimeout{0}; // 0: use the remaining total budget.
        std::chrono::milliseconds m_oMaxDelay{
            30000}; // Retry-After above this is not shortened/retried.
        // Optional GET body validation. Rejected final response is returned for caller error
        // handling.
        std::function<bool(const HttpResponse&)> m_fnAcceptResponse;
        // Called before waiting for a retry: next attempt number, libcurl error code (0 for
        // response/browser retries). No URLs, bodies or credentials are passed. Must not re-enter
        // the client.
        std::function<void(unsigned, int)> m_fnOnRetry;
    };
    // Opt-in for read-only GETs. Never retries POST, 401 or certificate errors.
    // 403/challenges require m_bRetryRefusedResponses; they are excluded by default.
    // HTTP/status and browser retries are disabled unless explicitly configured.
    // TransferOptions::timeout bounds all attempts and waits for each perform().
    WENKU8_API std::shared_ptr<HttpTransport> makeRetryingTransport(std::shared_ptr<HttpTransport>,
                                                                    RetryOptions = {});
    WENKU8_API std::string formEncode(const Fields&);
    enum class Language
    {
        Simplified = 0,
        Traditional = 1
    };
    enum class Sort
    {
        AllVisit,
        AllVote,
        MonthVisit,
        MonthVote,
        WeekVisit,
        WeekVote,
        DayVisit,
        DayVote,
        PostDate,
        LastUpdate,
        GoodNum,
        Size,
        FullFlag
    };
    enum class SearchType
    {
        Title,
        Author
    };
    enum class CachePolicy
    {
        NetworkOnly,
        PreferCache,
        CacheOnly,
        Refresh
    };
    enum class Protocol
    {
        LegacyApi,
        Website
    };
    struct Novel
    {
        int m_nId = 0;
        std::string m_strTitle, m_strAuthor, m_strStatus, m_strLastUpdate, m_strIntro,
            m_strPublisher, m_strLatestChapter;
        int m_nLatestChapterId = 0;
        std::uint64_t m_nHits = 0, m_nVotes = 0, m_nFavorites = 0, m_nLength = 0;
    };
    struct NovelPage
    {
        int m_nPages = 0;
        std::vector<Novel> m_vNovels;
    };
    struct Chapter
    {
        int m_nId = 0;
        std::string m_strTitle;
    };
    struct Volume
    {
        int m_nId = 0;
        std::string m_strTitle;
        std::vector<Chapter> m_vChapters;
    };
    struct Library
    {
        int m_nId = 0;
        std::string m_strName;
    };
    struct User
    {
        int m_nId = 0;
        std::string m_strName;
    }; // Historical login has no uid; 0 means unknown.
    struct LoginRequirements
    {
        bool m_bRequiresCaptcha = false;
    };
    struct UserProfile
    {
        int m_nId = 0;
        std::string m_strUsername, m_strNickname, m_strRank;
        std::uint64_t m_nScore = 0, m_nExperience = 0;
    };
    // Upstream's current authentication protocol is private. Supply a verified adapter;
    // Client never guesses authentication success from an HTTP 200 response.
    class WENKU8_API AuthenticationProtocol
    {
      public:
        virtual ~AuthenticationProtocol() = default;
        virtual HttpRequest loginRequest(const std::string&, const std::string&) const = 0;
        virtual User parseLogin(const HttpResponse&, const std::string& strRequestedName) const = 0;
        virtual HttpRequest logoutRequest() const = 0;
        virtual void validateLogout(const HttpResponse&) const = 0;
    };
    class WENKU8_API FileCache
    {
      public:
        explicit FileCache(std::filesystem::path oRoot);
        std::optional<std::string> read(const std::string& strKey,
                                        std::chrono::seconds oMaxAge) const;
        void write(const std::string& strKey, const std::string& strBytes);
        void remove(const std::string& strKey);

      private:
        std::filesystem::path m_oRoot;
        std::filesystem::path path(const std::string& strKey) const;
    };
    struct ClientOptions
    {
        // Explicit: the historical Android endpoint is not assumed alive.
        std::string m_strEndpoint;
        // Empty: earliest request-only envelope. Set explicitly for v1.x metadata.
        std::string m_strLegacyAppVersion;
        Language m_eLanguage = Language::Simplified;
        std::filesystem::path m_oCacheDirectory;
        std::chrono::seconds m_oCacheMaxAge{86400};
        TransferOptions m_oTransfer;
        std::shared_ptr<AuthenticationProtocol> m_pAuthentication;
        Protocol m_eProtocol = Protocol::LegacyApi;
        std::string m_strUserAgent = "Mozilla/5.0 (compatible; Wenku8CppSdk/0.2)";
    };
    // Synchronous, independent of Qt. Call on a GUI worker thread.
    // Do not call one Client concurrently. Separate clients may share a transport.
    class WENKU8_API Client
    {
      public:
        explicit Client(ClientOptions, std::shared_ptr<HttpTransport> pTransport = {});
        ~Client();
        Client(Client&&) noexcept;
        Client& operator=(Client&&) noexcept;
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;
        NovelPage list(Sort, int nPage = 1, CachePolicy = CachePolicy::PreferCache);
        NovelPage search(const std::string&, SearchType = SearchType::Title,
                         CachePolicy = CachePolicy::PreferCache);
        Novel novel(int nId, CachePolicy = CachePolicy::PreferCache);
        std::string introduction(int nId, CachePolicy = CachePolicy::PreferCache);
        std::vector<Volume> index(int nId, CachePolicy = CachePolicy::PreferCache);
        std::string chapter(int nNovelId, int nChapterId, CachePolicy = CachePolicy::PreferCache);
        std::string cover(int nId, CachePolicy = CachePolicy::PreferCache);
        std::vector<Library> libraries(CachePolicy = CachePolicy::PreferCache);
        NovelPage library(int nId, int nPage = 1, CachePolicy = CachePolicy::PreferCache);
        // Each chapter is cached separately; completed chapters survive cancellation.
        // For persisted task state, illustrations and a committed offline version,
        // use NovelDownloader from Wenku8Download.h.
        void downloadNovel(int nId, CachePolicy = CachePolicy::PreferCache);
        User login(const std::string&, const std::string&, const std::string& strCaptcha = {});
        // Website only: starts a fresh anonymous login session, without sending credentials.
        // Preserves hidden form fields/captcha session for the following login().
        LoginRequirements prepareLogin();
        // Website only. Fetch this before login to preserve the image's session.
        std::string captcha();
        UserProfile restoreSession(const std::string& strCookieHeader);
        void logout();
        std::optional<User> user() const;
        UserProfile userInfo();
        std::vector<Novel> bookshelf(); // Never stored in the public response cache.
        void addToBookshelf(int nId);
        void removeFromBookshelf(int nId);
        void vote(int nId);
        // Binary-safe, bounded by TransferOptions. Failure preserves destination.
        void download(const std::string& strUrl, const std::filesystem::path& oDestination);
        // Official independent asset node: complete UTF-8 TXT, not chapter-cache format.
        // Validates content before atomic replacement; does not require an account.
        void downloadTextPackage(int nNovelId, const std::filesystem::path& oDestination,
                                 CachePolicy = CachePolicy::PreferCache);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
} // namespace Wenku8
