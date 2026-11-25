// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Download.h"
#include "Wenku8Sdk.h"
#include <napi/native_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>

namespace
{
    using namespace Wenku8;
    using Clock = std::chrono::steady_clock;
    constexpr std::string_view strOrigin = "https://www.wenku8.net";
    constexpr std::array<std::string_view, 3> vAssetOrigins{
        "https://img.wenku8.com", "https://pic.wenku8.com", "https://pic.777743.xyz"};

    void require(napi_status eStatus)
    {
        if (eStatus != napi_ok)
        {
            throw std::invalid_argument("Invalid native bridge argument");
        }
    }

    napi_value stringValue(napi_env pEnv, std::string_view strValue)
    {
        napi_value pValue = nullptr;
        require(napi_create_string_utf8(pEnv, strValue.data(), strValue.size(), &pValue));
        return pValue;
    }

    napi_value numberValue(napi_env pEnv, int nValue)
    {
        napi_value pValue = nullptr;
        require(napi_create_int32(pEnv, nValue, &pValue));
        return pValue;
    }

    napi_value boolValue(napi_env pEnv, bool bValue)
    {
        napi_value pValue = nullptr;
        require(napi_get_boolean(pEnv, bValue, &pValue));
        return pValue;
    }

    napi_value object(napi_env pEnv)
    {
        napi_value pValue = nullptr;
        require(napi_create_object(pEnv, &pValue));
        return pValue;
    }

    napi_value undefined(napi_env pEnv)
    {
        napi_value pValue = nullptr;
        require(napi_get_undefined(pEnv, &pValue));
        return pValue;
    }

    void set(napi_env pEnv, napi_value pTarget, const char* pKey, napi_value pValue)
    {
        require(napi_set_named_property(pEnv, pTarget, pKey, pValue));
    }

    napi_value get(napi_env pEnv, napi_value pTarget, const char* pKey)
    {
        napi_value pValue = nullptr;
        require(napi_get_named_property(pEnv, pTarget, pKey, &pValue));
        return pValue;
    }

    napi_valuetype typeOf(napi_env pEnv, napi_value pValue)
    {
        if (!pValue)
        {
            return napi_undefined;
        }
        napi_valuetype eType;
        require(napi_typeof(pEnv, pValue, &eType));
        return eType;
    }

    std::string readString(napi_env pEnv, napi_value pValue, std::size_t nLimit)
    {
        if (typeOf(pEnv, pValue) == napi_undefined)
        {
            return {};
        }
        std::size_t nCount = 0;
        require(napi_get_value_string_utf8(pEnv, pValue, nullptr, 0, &nCount));
        if (nCount > nLimit)
        {
            throw std::invalid_argument("Native string exceeds limit");
        }
        std::string strValue(nCount + 1, '\0');
        require(
            napi_get_value_string_utf8(pEnv, pValue, strValue.data(), strValue.size(), &nCount));
        strValue.resize(nCount);
        if (strValue.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Invalid native string");
        }
        return strValue;
    }

    int readInteger(napi_env pEnv, napi_value pValue, int nDefault = 0)
    {
        if (typeOf(pEnv, pValue) == napi_undefined)
        {
            return nDefault;
        }
        double fValue = 0;
        require(napi_get_value_double(pEnv, pValue, &fValue));
        if (!std::isfinite(fValue) || fValue != std::trunc(fValue) || fValue < 0 ||
            fValue > (std::numeric_limits<int>::max)())
        {
            throw std::invalid_argument("Invalid native integer");
        }
        return static_cast<int>(fValue);
    }

    bool readBool(napi_env pEnv, napi_value pValue, bool bDefault)
    {
        if (typeOf(pEnv, pValue) == napi_undefined)
        {
            return bDefault;
        }
        bool bValue = false;
        require(napi_get_value_bool(pEnv, pValue, &bValue));
        return bValue;
    }

    void clearException(napi_env pEnv) noexcept
    {
        bool bPending = false;
        if (napi_is_exception_pending(pEnv, &bPending) == napi_ok && bPending)
        {
            napi_value pIgnored = nullptr;
            napi_get_and_clear_last_exception(pEnv, &pIgnored);
        }
    }

    napi_value errorValue(napi_env pEnv, const char* pCode, const char* pMessage) noexcept
    {
        try
        {
            clearException(pEnv);
            napi_value pError = nullptr;
            require(napi_create_error(pEnv, stringValue(pEnv, pCode), stringValue(pEnv, pMessage),
                                      &pError));
            return pError;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    napi_value invalidArgument(napi_env pEnv) noexcept
    {
        if (auto pError = errorValue(pEnv, "invalid", "请求参数无效"))
        {
            napi_throw(pEnv, pError);
        }
        return nullptr;
    }

    bool allowedUrl(std::string_view strUrl, bool bAsset)
    {
        auto fnContains = [strUrl](std::string_view strBase)
        {
            return strUrl.size() > strBase.size() && strUrl.substr(0, strBase.size()) == strBase &&
                   strUrl[strBase.size()] == '/';
        };
        return bAsset ? std::any_of(vAssetOrigins.begin(), vAssetOrigins.end(), fnContains)
                      : fnContains(strOrigin);
    }

    struct PendingResponse
    {
        napi_env m_pEnv;
        std::size_t m_nLimit;
        bool m_bAsset;
        std::mutex m_oMutex;
        std::condition_variable m_oReady;
        std::optional<HttpResponse> m_oResponse;
        bool m_bDone = false;
        bool m_bTooLarge = false;

        PendingResponse(napi_env pEnv, std::size_t nLimit, bool bAsset)
            : m_pEnv(pEnv), m_nLimit(nLimit), m_bAsset(bAsset)
        {
        }
    };

    std::mutex oPendingMutex;
    std::unordered_map<int, std::shared_ptr<PendingResponse>> vPending;
    int nLastId = 0;

    class PendingRequest
    {
      public:
        explicit PendingRequest(const std::shared_ptr<PendingResponse>& pPending)
        {
            std::lock_guard<std::mutex> oGuard(oPendingMutex);
            do
            {
                nLastId = nLastId == (std::numeric_limits<int>::max)() ? 1 : nLastId + 1;
            } while (vPending.count(nLastId));
            m_nId = nLastId;
            vPending.emplace(m_nId, pPending);
        }

        ~PendingRequest()
        {
            std::lock_guard<std::mutex> oGuard(oPendingMutex);
            vPending.erase(m_nId);
        }

        PendingRequest(const PendingRequest&) = delete;
        PendingRequest& operator=(const PendingRequest&) = delete;

        int id() const
        {
            return m_nId;
        }

      private:
        int m_nId = 0;
    };

    std::shared_ptr<PendingResponse> findPending(napi_env pEnv, int nId)
    {
        std::lock_guard<std::mutex> oGuard(oPendingMutex);
        auto oEntry = vPending.find(nId);
        return oEntry != vPending.end() && oEntry->second->m_pEnv == pEnv ? oEntry->second
                                                                          : nullptr;
    }

    void finishPending(const std::shared_ptr<PendingResponse>& pPending,
                       std::optional<HttpResponse> oResponse = std::nullopt, bool bTooLarge = false)
    {
        {
            std::lock_guard<std::mutex> oGuard(pPending->m_oMutex);
            if (pPending->m_bDone)
            {
                return;
            }
            pPending->m_bDone = true;
            pPending->m_bTooLarge = bTooLarge;
            pPending->m_oResponse = std::move(oResponse);
        }
        pPending->m_oReady.notify_one();
    }

    struct BrowserCall
    {
        std::shared_ptr<PendingResponse> m_pPending;
        int m_nId;
        std::string m_strUrl, m_strMethod, m_strBody;
        bool m_bFollow;
    };

    void dispatchBrowser(napi_env pEnv, napi_value pCallback, void*, void* pData)
    {
        std::unique_ptr<BrowserCall> pCall(static_cast<BrowserCall*>(pData));
        if (pEnv && pCallback)
        {
            try
            {
                const napi_value vArgs[]{stringValue(pEnv, pCall->m_strUrl),
                                         stringValue(pEnv, pCall->m_strMethod),
                                         stringValue(pEnv, pCall->m_strBody),
                                         numberValue(pEnv, pCall->m_nId),
                                         boolValue(pEnv, pCall->m_bFollow),
                                         boolValue(pEnv, pCall->m_pPending->m_bAsset)};
                if (napi_call_function(pEnv, undefined(pEnv), pCallback, std::size(vArgs), vArgs,
                                       nullptr) == napi_ok)
                {
                    return;
                }
            }
            catch (...)
            {
            }
            clearException(pEnv);
        }
        finishPending(pCall->m_pPending);
    }

    class BrowserTransport final : public HttpTransport
    {
      public:
        BrowserTransport(napi_env pEnv, napi_threadsafe_function pCallback, bool bAsset)
            : m_pEnv(pEnv), m_pCallback(pCallback), m_bAsset(bAsset)
        {
        }

        bool supportsSameOriginGetRedirects() const noexcept override
        {
            return !m_bAsset;
        }

        // ArkWeb owns the browsing session. The SDK never imports or clears its cookies.
        void clearCookies() override
        {
        }

        HttpResponse perform(const HttpRequest& oRequest, const TransferOptions& oOptions) override
        {
            if (!allowedUrl(oRequest.m_strUrl, m_bAsset) || oRequest.m_strMethod != "GET")
            {
                throw Error(ErrorCode::InvalidArgument, "Unexpected browser request");
            }
            if (!m_pCallback)
            {
                throw Error(ErrorCode::Network, "Browser transport is unavailable");
            }
            const auto oDeadline = Clock::now() + oOptions.m_oTimeout;
            auto pPending =
                std::make_shared<PendingResponse>(m_pEnv, oOptions.m_nMaxResponseBytes, m_bAsset);
            const PendingRequest oEntry(pPending);
            auto pCall = std::make_unique<BrowserCall>(
                BrowserCall{pPending, oEntry.id(), oRequest.m_strUrl, oRequest.m_strMethod,
                            oRequest.m_strBody, oOptions.m_bFollowRedirects});
            if (napi_call_threadsafe_function(m_pCallback, pCall.get(), napi_tsfn_nonblocking) !=
                napi_ok)
            {
                throw Error(ErrorCode::Network, "Browser dispatch failed");
            }
            pCall.release();
            std::unique_lock<std::mutex> oLock(pPending->m_oMutex);
            while (!pPending->m_bDone && Clock::now() < oDeadline)
            {
                if (oOptions.m_fnProgress && !oOptions.m_fnProgress(0, 0))
                {
                    throw Error(ErrorCode::Cancelled, "Browser request cancelled");
                }
                pPending->m_oReady.wait_until(
                    oLock, std::min(oDeadline, Clock::now() + std::chrono::milliseconds(100)));
            }
            if (pPending->m_bTooLarge)
            {
                throw Error(ErrorCode::ResponseTooLarge, "Browser response exceeds limit");
            }
            if (!pPending->m_oResponse)
            {
                throw Error(ErrorCode::Network, "Browser request failed or timed out");
            }
            return std::move(*pPending->m_oResponse);
        }

      private:
        napi_env m_pEnv;
        napi_threadsafe_function m_pCallback;
        bool m_bAsset;
    };

    napi_value respond(napi_env pEnv, napi_callback_info pInfo)
    {
        std::array<napi_value, 6> vArgs{};
        std::size_t nCount = vArgs.size();
        std::shared_ptr<PendingResponse> pPending;
        try
        {
            require(napi_get_cb_info(pEnv, pInfo, &nCount, vArgs.data(), nullptr, nullptr));
            const int nId = readInteger(pEnv, vArgs[0]);
            pPending = findPending(pEnv, nId);
            if (!pPending)
            {
                return undefined(pEnv);
            }
            HttpResponse oResponse;
            oResponse.m_nStatus = readInteger(pEnv, vArgs[1]);
            oResponse.m_strEffectiveUrl = readString(pEnv, vArgs[3], 8192);
            if (oResponse.m_nStatus < 100 || oResponse.m_nStatus > 599 ||
                (!oResponse.m_strEffectiveUrl.empty() &&
                 !allowedUrl(oResponse.m_strEffectiveUrl, pPending->m_bAsset)))
            {
                finishPending(pPending);
                return undefined(pEnv);
            }
            void* pBody = nullptr;
            std::size_t nBytes = 0;
            require(napi_get_arraybuffer_info(pEnv, vArgs[2], &pBody, &nBytes));
            if (nBytes > pPending->m_nLimit)
            {
                finishPending(pPending, std::nullopt, true);
                return undefined(pEnv);
            }
            if (nBytes)
            {
                oResponse.m_strBody.assign(static_cast<const char*>(pBody), nBytes);
            }
            auto strContentType = readString(pEnv, vArgs[4], 512);
            if (!strContentType.empty())
            {
                oResponse.m_vHeaders.emplace_back("Content-Type", std::move(strContentType));
            }
            auto strLocation = readString(pEnv, vArgs[5], 8192);
            if (!strLocation.empty())
            {
                oResponse.m_vHeaders.emplace_back("Location", std::move(strLocation));
            }
            finishPending(pPending, std::move(oResponse));
            return undefined(pEnv);
        }
        catch (...)
        {
            if (pPending)
            {
                finishPending(pPending);
            }
            return invalidArgument(pEnv);
        }
    }

    napi_value reject(napi_env pEnv, napi_callback_info pInfo)
    {
        try
        {
            napi_value pId = nullptr;
            std::size_t nCount = 1;
            require(napi_get_cb_info(pEnv, pInfo, &nCount, &pId, nullptr, nullptr));
            if (auto pPending = findPending(pEnv, readInteger(pEnv, pId)))
            {
                finishPending(pPending);
            }
            return undefined(pEnv);
        }
        catch (...)
        {
            return invalidArgument(pEnv);
        }
    }

    struct Request
    {
        std::string m_strOperation, m_strQuery, m_strCacheDir, m_strAccount, m_strUrl;
        int m_nId = 0, m_nChapterId = 0, m_nPage = 1, m_nTimeoutMs = 0;
        bool m_bImages = true;
    };

    Request readRequest(napi_env pEnv, napi_value pRequest)
    {
        Request oRequest;
        oRequest.m_strOperation = readString(pEnv, get(pEnv, pRequest, "operation"), 32);
        oRequest.m_strQuery = readString(pEnv, get(pEnv, pRequest, "query"), 256);
        oRequest.m_strCacheDir = readString(pEnv, get(pEnv, pRequest, "cacheDir"), 4096);
        oRequest.m_strAccount = readString(pEnv, get(pEnv, pRequest, "account"), 256);
        oRequest.m_strUrl = readString(pEnv, get(pEnv, pRequest, "url"), 8192);
        oRequest.m_nId = readInteger(pEnv, get(pEnv, pRequest, "id"));
        oRequest.m_nChapterId = readInteger(pEnv, get(pEnv, pRequest, "chapterId"));
        oRequest.m_nPage = readInteger(pEnv, get(pEnv, pRequest, "page"), 1);
        oRequest.m_nTimeoutMs = readInteger(pEnv, get(pEnv, pRequest, "timeoutMs"));
        oRequest.m_bImages = readBool(pEnv, get(pEnv, pRequest, "includeImages"), true);
        if (!oRequest.m_strCacheDir.empty() &&
            !std::filesystem::u8path(oRequest.m_strCacheDir).is_absolute())
        {
            throw std::invalid_argument("Cache directory must be absolute");
        }
        return oRequest;
    }

    struct Result
    {
        std::optional<std::vector<Novel>> m_vNovels;
        std::optional<int> m_nPages;
        std::optional<Novel> m_oNovel;
        std::optional<std::vector<Volume>> m_vVolumes;
        std::optional<std::string> m_strText, m_strUser, m_strBytes;
        std::optional<DownloadStatus> m_oDownload;
    };

    struct Progress
    {
        int m_nCompleted, m_nTotal;
    };

    void dispatchProgress(napi_env pEnv, napi_value pCallback, void*, void* pData)
    {
        std::unique_ptr<Progress> pProgress(static_cast<Progress*>(pData));
        if (!pEnv || !pCallback)
        {
            return;
        }
        try
        {
            const napi_value vArgs[]{numberValue(pEnv, pProgress->m_nCompleted),
                                     numberValue(pEnv, pProgress->m_nTotal)};
            napi_call_function(pEnv, undefined(pEnv), pCallback, std::size(vArgs), vArgs, nullptr);
        }
        catch (...)
        {
        }
        clearException(pEnv);
    }

    struct Operation
    {
        napi_env m_pEnv = nullptr;
        napi_async_work m_pWork = nullptr;
        napi_deferred m_pDeferred = nullptr;
        napi_threadsafe_function m_pBrowser = nullptr, m_pProgress = nullptr;
        Request m_oRequest;
        Result m_oResult;
        const char* m_pErrorCode = nullptr;
        const char* m_pErrorMessage = nullptr;
    };

    void requireAccount(Client& oClient, const std::string& strAccount)
    {
        const auto oProfile = oClient.userInfo();
        if (!strAccount.empty() && oProfile.m_strUsername != strAccount)
        {
            throw Error(ErrorCode::Authentication, "Browsing account changed");
        }
    }

    std::mutex oDownloadMutex;
    std::set<std::pair<std::filesystem::path, int>> vDownloads;

    class DownloadLease
    {
      public:
        DownloadLease(const std::filesystem::path& oDirectory, int nId) : m_oKey(oDirectory, nId)
        {
            std::lock_guard<std::mutex> oGuard(oDownloadMutex);
            if (!vDownloads.insert(m_oKey).second)
            {
                throw Error(ErrorCode::InvalidArgument, "This book is already downloading");
            }
        }

        ~DownloadLease()
        {
            std::lock_guard<std::mutex> oGuard(oDownloadMutex);
            vDownloads.erase(m_oKey);
        }

        DownloadLease(const DownloadLease&) = delete;
        DownloadLease& operator=(const DownloadLease&) = delete;

      private:
        std::pair<std::filesystem::path, int> m_oKey;
    };

    std::vector<Novel> cachedNovels(const NovelDownloader& oStore)
    {
        const auto oBooks = oStore.bookDirectory(1).parent_path();
        std::vector<Novel> vNovels;
        std::error_code oError;
        const std::filesystem::directory_iterator oEnd;
        std::size_t nVisited = 0;
        for (std::filesystem::directory_iterator oEntry(oBooks, oError);
             !oError && oEntry != oEnd && nVisited < 4096; oEntry.increment(oError), ++nVisited)
        {
            if (!oEntry->is_directory(oError))
            {
                continue;
            }
            const auto strName = oEntry->path().filename().u8string();
            if (strName.empty() || strName.size() > 10 ||
                strName.find_first_not_of("0123456789") != std::string::npos)
            {
                continue;
            }
            try
            {
                const int nId = std::stoi(strName);
                if (nId > 0 && strName == std::to_string(nId))
                {
                    vNovels.push_back(oStore.novel(nId));
                }
            }
            catch (const Error&)
            {
                // In-progress downloads do not have a committed book manifest.
            }
            catch (const std::out_of_range&)
            {
            }
        }
        std::sort(vNovels.begin(), vNovels.end(),
                  [](const Novel& oLeft, const Novel& oRight)
                  {
                      return oLeft.m_nId > oRight.m_nId;
                  });
        return vNovels;
    }

    void runOperation(Operation& oWork)
    {
        const auto& oRequest = oWork.m_oRequest;
        auto& oResult = oWork.m_oResult;
        const std::string& strName = oRequest.m_strOperation;
        ClientOptions oOptions;
        oOptions.m_strEndpoint = strOrigin;
        oOptions.m_eProtocol = Protocol::Website;
        const auto nDefaultTimeout = strName == "profile" ? 5000 : 20000;
        const auto oTimeout = std::chrono::milliseconds(
            oRequest.m_nTimeoutMs ? std::clamp(oRequest.m_nTimeoutMs, 1000, 45000)
                                  : nDefaultTimeout);
        oOptions.m_oTransfer.m_oTimeout = oTimeout;
        oOptions.m_oTransfer.m_nMaxResponseBytes = 8 * 1024 * 1024;
        auto pContent = std::make_shared<BrowserTransport>(oWork.m_pEnv, oWork.m_pBrowser, false);
        auto pAssets = std::make_shared<BrowserTransport>(oWork.m_pEnv, oWork.m_pBrowser, true);
        RetryPolicy oRetry;
        oRetry.m_oOptions = defaultRetryOptions(RetryProfile::Browser);
        if (strName == "profile")
        {
            oRetry.m_bEnabled = false;
        }
        if (strName == "cover")
        {
            if (!oRequest.m_strCacheDir.empty())
            {
                oOptions.m_oCacheDirectory =
                    std::filesystem::u8path(oRequest.m_strCacheDir) / "covers";
            }
            Client oClient(oOptions, pAssets, oRetry);
            oResult.m_strBytes =
                oClient.cover(oRequest.m_nId,
                              oWork.m_pBrowser ? CachePolicy::PreferCache : CachePolicy::CacheOnly);
            return;
        }

        if (strName == "listCache" || strName == "cachedBook" || strName == "cachedChapter" ||
            strName == "cachedImage" || strName == "download")
        {
            if (oRequest.m_strCacheDir.empty())
            {
                throw std::invalid_argument("Application cache directory required");
            }
            DownloadStoreOptions oStoreOptions;
            oStoreOptions.m_oDirectory = std::filesystem::u8path(oRequest.m_strCacheDir) / "books";
            oStoreOptions.m_oAssets.m_oTimeout = oTimeout;
            oStoreOptions.m_oAssets.m_nMaxResponseBytes = 8 * 1024 * 1024;
            oStoreOptions.m_vImageOrigins.assign(vAssetOrigins.begin(), vAssetOrigins.end());
            NovelDownloader oStore(oOptions, oStoreOptions, pContent, pAssets, oRetry);
            if (strName == "listCache")
            {
                oResult.m_vNovels = cachedNovels(oStore);
            }
            else if (strName == "cachedBook")
            {
                oResult.m_oNovel = oStore.novel(oRequest.m_nId);
                oResult.m_vVolumes = oStore.index(oRequest.m_nId);
            }
            else if (strName == "cachedChapter")
            {
                oResult.m_strText = oStore.chapter(oRequest.m_nId, oRequest.m_nChapterId);
            }
            else if (strName == "cachedImage")
            {
                oResult.m_strBytes = oStore.image(oRequest.m_nId, oRequest.m_strUrl);
            }
            else
            {
                const DownloadLease oLease(oStoreOptions.m_oDirectory.lexically_normal(),
                                           oRequest.m_nId);
                Client oClient(oOptions, pContent, oRetry);
                requireAccount(oClient, oRequest.m_strAccount);
                DownloadRequest oDownload;
                oDownload.m_bIncludeImages = oRequest.m_bImages;
                oDownload.m_bIncludeCover = false;
                Progress oLast{-1, -1};
                oDownload.m_fnProgress = [&oWork, &oLast](const DownloadStatus& oStatus)
                {
                    const Progress oNext{static_cast<int>(oStatus.m_nChaptersCompleted),
                                         static_cast<int>(oStatus.m_nChaptersTotal)};
                    if (oWork.m_pProgress && (oNext.m_nCompleted != oLast.m_nCompleted ||
                                              oNext.m_nTotal != oLast.m_nTotal))
                    {
                        auto pProgress = std::make_unique<Progress>(oNext);
                        if (napi_call_threadsafe_function(oWork.m_pProgress, pProgress.get(),
                                                          napi_tsfn_nonblocking) == napi_ok)
                        {
                            pProgress.release();
                        }
                    }
                    oLast = oNext;
                    return true;
                };
                oResult.m_oDownload = oStore.download(oRequest.m_nId, oDownload);
            }
            return;
        }

        auto pTransport = strName == "book" || strName == "shelf" || strName == "addToShelf"
                              ? makeDeadlineTransport(makeSdkTransport(pContent, oRetry), oTimeout)
                              : makeSdkTransport(pContent, oRetry);
        Client oClient(oOptions, pTransport, RetryPolicy{false});
        if (strName == "profile")
        {
            oResult.m_strUser = oClient.userInfo().m_strUsername;
        }
        else if (strName == "shelf")
        {
            requireAccount(oClient, oRequest.m_strAccount);
            oResult.m_vNovels = oClient.bookshelf();
        }
        else if (strName == "latest" || strName == "search")
        {
            auto oPage = strName == "latest" ? oClient.list(Sort::LastUpdate, oRequest.m_nPage)
                                             : oClient.search(oRequest.m_strQuery);
            oResult.m_nPages = oPage.m_nPages;
            oResult.m_vNovels = std::move(oPage.m_vNovels);
        }
        else if (strName == "book")
        {
            oResult.m_oNovel = oClient.novel(oRequest.m_nId);
            oResult.m_vVolumes = oClient.index(oRequest.m_nId);
        }
        else if (strName == "chapter")
        {
            oResult.m_strText = oClient.chapter(oRequest.m_nId, oRequest.m_nChapterId);
        }
        else if (strName == "addToShelf")
        {
            requireAccount(oClient, oRequest.m_strAccount);
            oClient.addToBookshelf(oRequest.m_nId);
        }
        else
        {
            throw std::invalid_argument("Unknown native operation");
        }
    }

    void execute(napi_env, void* pData)
    {
        auto& oWork = *static_cast<Operation*>(pData);
        try
        {
            runOperation(oWork);
        }
        catch (const Error& oError)
        {
            switch (oError.code())
            {
            case ErrorCode::Authentication:
                oWork.m_pErrorCode = "authentication";
                oWork.m_pErrorMessage = "请在「我的」登录文库";
                break;
            case ErrorCode::BrowserVerificationRequired:
            case ErrorCode::CaptchaRequired:
            case ErrorCode::SiteAccessDenied:
                oWork.m_pErrorCode = "verification";
                oWork.m_pErrorMessage = "请在网页登录页完成文库验证";
                break;
            case ErrorCode::CacheMiss:
                oWork.m_pErrorCode = "cache";
                oWork.m_pErrorMessage = "此内容没有可读取的缓存";
                break;
            case ErrorCode::Cancelled:
                oWork.m_pErrorCode = "cancelled";
                oWork.m_pErrorMessage = "操作已取消";
                break;
            case ErrorCode::Network:
            case ErrorCode::Http:
                oWork.m_pErrorCode = oError.httpStatus() == 401 ? "authentication" : "network";
                oWork.m_pErrorMessage = oError.httpStatus() == 401 ? "请在「我的」登录文库"
                                                                   : "网络访问失败，请稍后重试";
                break;
            default:
                oWork.m_pErrorCode = "sdk";
                oWork.m_pErrorMessage = "暂时无法读取数据";
                break;
            }
        }
        catch (const std::invalid_argument&)
        {
            oWork.m_pErrorCode = "invalid";
            oWork.m_pErrorMessage = "请求参数无效";
        }
        catch (...)
        {
            oWork.m_pErrorCode = "sdk";
            oWork.m_pErrorMessage = "暂时无法读取数据";
        }
    }

    napi_value novelValue(napi_env pEnv, const Novel& oNovel)
    {
        auto pNovel = object(pEnv);
        set(pEnv, pNovel, "id", numberValue(pEnv, oNovel.m_nId));
        set(pEnv, pNovel, "title", stringValue(pEnv, oNovel.m_strTitle));
        set(pEnv, pNovel, "author", stringValue(pEnv, oNovel.m_strAuthor));
        set(pEnv, pNovel, "status", stringValue(pEnv, oNovel.m_strStatus));
        set(pEnv, pNovel, "intro", stringValue(pEnv, oNovel.m_strIntro));
        set(pEnv, pNovel, "latestChapter", stringValue(pEnv, oNovel.m_strLatestChapter));
        return pNovel;
    }

    template <typename Item, typename Convert>
    napi_value arrayValue(napi_env pEnv, const std::vector<Item>& vItems, Convert fnConvert)
    {
        napi_value pArray = nullptr;
        require(napi_create_array_with_length(pEnv, vItems.size(), &pArray));
        for (std::uint32_t nIndex = 0; nIndex < vItems.size(); ++nIndex)
        {
            require(napi_set_element(pEnv, pArray, nIndex, fnConvert(pEnv, vItems[nIndex])));
        }
        return pArray;
    }

    napi_value volumeValue(napi_env pEnv, const Volume& oVolume)
    {
        auto pVolume = object(pEnv);
        set(pEnv, pVolume, "id", numberValue(pEnv, oVolume.m_nId));
        set(pEnv, pVolume, "title", stringValue(pEnv, oVolume.m_strTitle));
        set(pEnv, pVolume, "chapters",
            arrayValue(pEnv, oVolume.m_vChapters,
                       [](napi_env pChapterEnv, const Chapter& oChapter)
                       {
                           auto pChapter = object(pChapterEnv);
                           set(pChapterEnv, pChapter, "id",
                               numberValue(pChapterEnv, oChapter.m_nId));
                           set(pChapterEnv, pChapter, "title",
                               stringValue(pChapterEnv, oChapter.m_strTitle));
                           return pChapter;
                       }));
        return pVolume;
    }

    napi_value resultValue(napi_env pEnv, const Result& oResult)
    {
        auto pResult = object(pEnv);
        if (oResult.m_vNovels)
            set(pEnv, pResult, "novels", arrayValue(pEnv, *oResult.m_vNovels, novelValue));
        if (oResult.m_nPages)
            set(pEnv, pResult, "pages", numberValue(pEnv, *oResult.m_nPages));
        if (oResult.m_oNovel)
            set(pEnv, pResult, "novel", novelValue(pEnv, *oResult.m_oNovel));
        if (oResult.m_vVolumes)
            set(pEnv, pResult, "volumes", arrayValue(pEnv, *oResult.m_vVolumes, volumeValue));
        if (oResult.m_strText)
            set(pEnv, pResult, "text", stringValue(pEnv, *oResult.m_strText));
        if (oResult.m_strUser)
            set(pEnv, pResult, "username", stringValue(pEnv, *oResult.m_strUser));
        if (oResult.m_strBytes)
        {
            napi_value pBuffer = nullptr;
            void* pData = nullptr;
            require(napi_create_arraybuffer(pEnv, oResult.m_strBytes->size(), &pData, &pBuffer));
            if (!oResult.m_strBytes->empty())
            {
                std::memcpy(pData, oResult.m_strBytes->data(), oResult.m_strBytes->size());
            }
            set(pEnv, pResult, "bytes", pBuffer);
        }
        if (oResult.m_oDownload)
        {
            set(pEnv, pResult, "chaptersCompleted",
                numberValue(pEnv, static_cast<int>(oResult.m_oDownload->m_nChaptersCompleted)));
            set(pEnv, pResult, "chaptersTotal",
                numberValue(pEnv, static_cast<int>(oResult.m_oDownload->m_nChaptersTotal)));
        }
        return pResult;
    }

    void releaseCallbacks(Operation& oWork, napi_threadsafe_function_release_mode eMode)
    {
        if (oWork.m_pBrowser)
            napi_release_threadsafe_function(oWork.m_pBrowser, eMode);
        if (oWork.m_pProgress)
            napi_release_threadsafe_function(oWork.m_pProgress, eMode);
    }

    void complete(napi_env pEnv, napi_status eStatus, void* pData)
    {
        std::unique_ptr<Operation> pWork(static_cast<Operation*>(pData));
        releaseCallbacks(*pWork, napi_tsfn_release);
        if (eStatus == napi_ok && !pWork->m_pErrorCode)
        {
            try
            {
                require(napi_resolve_deferred(pEnv, pWork->m_pDeferred,
                                              resultValue(pEnv, pWork->m_oResult)));
            }
            catch (...)
            {
                if (auto pError = errorValue(pEnv, "sdk", "暂时无法读取数据"))
                    napi_reject_deferred(pEnv, pWork->m_pDeferred, pError);
            }
        }
        else
        {
            const auto pCode = pWork->m_pErrorCode ? pWork->m_pErrorCode : "cancelled";
            const auto pMessage = pWork->m_pErrorMessage ? pWork->m_pErrorMessage : "操作已取消";
            if (auto pError = errorValue(pEnv, pCode, pMessage))
                napi_reject_deferred(pEnv, pWork->m_pDeferred, pError);
        }
        napi_delete_async_work(pEnv, pWork->m_pWork);
    }

    void createCallback(napi_env pEnv, napi_value pValue, std::string_view strName,
                        napi_threadsafe_function_call_js fnCall, napi_threadsafe_function& pTarget)
    {
        if (typeOf(pEnv, pValue) != napi_undefined)
        {
            require(napi_create_threadsafe_function(pEnv, pValue, nullptr,
                                                    stringValue(pEnv, strName), 8, 1, nullptr,
                                                    nullptr, nullptr, fnCall, &pTarget));
        }
    }

    napi_value run(napi_env pEnv, napi_callback_info pInfo)
    {
        auto pWork = std::make_unique<Operation>();
        try
        {
            std::array<napi_value, 3> vArgs{};
            std::size_t nCount = vArgs.size();
            require(napi_get_cb_info(pEnv, pInfo, &nCount, vArgs.data(), nullptr, nullptr));
            if (!nCount)
            {
                return invalidArgument(pEnv);
            }
            pWork->m_pEnv = pEnv;
            pWork->m_oRequest = readRequest(pEnv, vArgs[0]);
            createCallback(pEnv, vArgs[1], "Wenku8 browser request", dispatchBrowser,
                           pWork->m_pBrowser);
            createCallback(pEnv, vArgs[2], "Wenku8 download progress", dispatchProgress,
                           pWork->m_pProgress);
            napi_value pPromise = nullptr;
            require(napi_create_promise(pEnv, &pWork->m_pDeferred, &pPromise));
            require(napi_create_async_work(pEnv, nullptr, stringValue(pEnv, "Wenku8 SDK"), execute,
                                           complete, pWork.get(), &pWork->m_pWork));
            require(napi_queue_async_work(pEnv, pWork->m_pWork));
            pWork.release();
            return pPromise;
        }
        catch (...)
        {
            releaseCallbacks(*pWork, napi_tsfn_abort);
            if (pWork->m_pWork)
            {
                napi_delete_async_work(pEnv, pWork->m_pWork);
            }
            return invalidArgument(pEnv);
        }
    }

    napi_value version(napi_env pEnv, napi_callback_info)
    {
        try
        {
            return stringValue(pEnv, sdkVersion());
        }
        catch (...)
        {
            return invalidArgument(pEnv);
        }
    }

    napi_value init(napi_env pEnv, napi_value pExports)
    {
        const napi_property_descriptor vMethods[]{
            {"sdkVersion", nullptr, version, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"run", nullptr, run, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"respond", nullptr, respond, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"reject", nullptr, reject, nullptr, nullptr, nullptr, napi_default, nullptr}};
        if (napi_define_properties(pEnv, pExports, std::size(vMethods), vMethods) != napi_ok)
        {
            return nullptr;
        }
        return pExports;
    }

    napi_module oModule{1, 0, nullptr, init, "wenku8_bridge", nullptr, {}};
} // namespace

extern "C" __attribute__((constructor)) void registerWenku8Bridge()
{
    napi_module_register(&oModule);
}
