// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Download.h"
#include "Storage.h"
#include "tinyxml2.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <curl/curl.h>
#include <libxml/uri.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <regex>
#include <thread>
#include <ctime>

namespace Wenku8
{
    namespace
    {
        constexpr std::uint64_t nObjectLimit = 128ULL * 1024 * 1024;
        struct RequestGate
        {
            std::chrono::steady_clock::time_point m_oNext{};
            std::chrono::milliseconds m_oInterval{};
        };
        class TaskTransport final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;
            std::shared_ptr<RequestGate> m_pGate;
            unsigned m_nRetries;
            std::chrono::seconds m_oMaxWait;
            void pause(std::chrono::steady_clock::time_point oUntil,
                       const TransferOptions& oOptions)
            {
                while (std::chrono::steady_clock::now() < oUntil)
                {
                    if (oOptions.m_fnProgress && !oOptions.m_fnProgress(0, 0))
                        throw Error(ErrorCode::Cancelled, "Download wait cancelled");
                    auto oLeft = std::chrono::duration_cast<std::chrono::milliseconds>(
                        oUntil - std::chrono::steady_clock::now());
                    if (oLeft.count() > 0)
                        std::this_thread::sleep_for(
                            (std::min)(oLeft, std::chrono::milliseconds(25)));
                }
            }

          public:
            TaskTransport(std::shared_ptr<HttpTransport> pInner, std::shared_ptr<RequestGate> pGate,
                          const DownloadRequest& oRequest)
                : m_pInner(std::move(pInner)), m_pGate(std::move(pGate)),
                  m_nRetries(oRequest.m_nRateLimitRetries), m_oMaxWait(oRequest.m_oRateLimitWait)
            {
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return m_pInner->supportsSameOriginGetRedirects();
            }
            void clearCookies() override
            {
                m_pInner->clearCookies();
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                for (unsigned nAttempt = 0;; ++nAttempt)
                {
                    pause(m_pGate->m_oNext, oOptions);
                    m_pGate->m_oNext = std::chrono::steady_clock::now() + m_pGate->m_oInterval;
                    auto oResponse = m_pInner->perform(oRequest, oOptions);
                    if (oResponse.m_nStatus != 429 || oRequest.m_strMethod != "GET" ||
                        nAttempt >= m_nRetries)
                        return oResponse;
                    auto oDelay = m_oMaxWait;
                    for (const auto& oHeader : oResponse.m_vHeaders)
                    {
                        auto strName = oHeader.first;
                        std::transform(strName.begin(), strName.end(), strName.begin(),
                                       [](unsigned char nC)
                                       {
                                           return static_cast<char>(std::tolower(nC));
                                       });
                        if (strName != "retry-after")
                            continue;
                        std::uint64_t nSeconds{};
                        const auto& strValue = oHeader.second;
                        const auto oParsed = std::from_chars(
                            strValue.data(), strValue.data() + strValue.size(), nSeconds);
                        if (oParsed.ec == std::errc{} &&
                            oParsed.ptr == strValue.data() + strValue.size())
                        {
                            if (nSeconds > static_cast<std::uint64_t>(m_oMaxWait.count()))
                                return oResponse;
                            oDelay = std::chrono::seconds(nSeconds);
                        }
                        else if (const auto nDate = curl_getdate(strValue.c_str(), nullptr);
                                 nDate != -1)
                        {
                            const auto nDelta = nDate - std::time(nullptr);
                            if (nDelta > m_oMaxWait.count())
                                return oResponse;
                            oDelay = std::chrono::seconds(nDelta > 0 ? nDelta : 0);
                        }
                    }
                    pause(std::chrono::steady_clock::now() + oDelay, oOptions);
                }
            }
        };
        std::string hash(const std::string& strBytes)
        {
            // Accidental corruption detection and namespacing, not a cryptographic signature.
            std::uint64_t nValue = 14695981039346656037ULL;
            for (unsigned char nC : strBytes)
            {
                nValue ^= nC;
                nValue *= 1099511628211ULL;
            }
            std::ostringstream oOut;
            oOut << std::hex << std::setw(16) << std::setfill('0') << nValue;
            return oOut.str();
        }
        std::string read(const std::filesystem::path& oFile, std::uint64_t nLimit)
        {
            std::error_code oEc;
            auto nSize = std::filesystem::file_size(oFile, oEc);
            if (oEc || nSize > nLimit)
                throw Error(ErrorCode::CacheMiss, "Missing or oversized download-store file");
            std::ifstream oInput(oFile, std::ios::binary);
            std::string strBytes(static_cast<std::size_t>(nSize), '\0');
            if (!oInput ||
                (nSize && !oInput.read(&strBytes[0], static_cast<std::streamsize>(nSize))))
                throw Error(ErrorCode::Io, "Cannot read download-store file");
            return strBytes;
        }
        std::string objectName(const std::string& strBytes)
        {
            return hash(strBytes) + '-' + std::to_string(strBytes.size()) + ".bin";
        }
        std::string attr(const tinyxml2::XMLElement* pE, const char* pName)
        {
            auto pS = pE->Attribute(pName);
            return pS ? pS : "";
        }
        std::uint64_t number(const std::string& strText)
        {
            std::uint64_t nN{};
            auto oR = std::from_chars(strText.data(), strText.data() + strText.size(), nN);
            if (strText.empty() || oR.ec != std::errc{} ||
                oR.ptr != strText.data() + strText.size())
                throw Error(ErrorCode::Parse, "Invalid download manifest number");
            return nN;
        }
        int id(const std::string& strText)
        {
            auto nN = number(strText);
            if (!nN || nN > 2147483647)
                throw Error(ErrorCode::Parse, "Invalid download manifest ID");
            return static_cast<int>(nN);
        }
        void positive(int nValue)
        {
            if (nValue <= 0)
                throw Error(ErrorCode::InvalidArgument, "Invalid novel/chapter ID");
        }
        void text(const std::string& strBytes)
        {
            if (strBytes.empty() || strBytes.find('\0') != std::string::npos ||
                !xmlCheckUTF8(BAD_CAST strBytes.c_str()))
                throw Error(ErrorCode::Parse, "Invalid UTF-8 chapter");
        }
        std::string origin(const std::string& strUrl)
        {
            if (strUrl.empty() || strUrl.find('\0') != std::string::npos ||
                strUrl.find_first_of(" \r\n\t") != std::string::npos)
                throw Error(ErrorCode::InvalidArgument, "Invalid image URL");
            std::unique_ptr<xmlURI, decltype(&xmlFreeURI)> pUri(xmlParseURI(strUrl.c_str()),
                                                                xmlFreeURI);
            if (!pUri || !pUri->scheme || !pUri->server || pUri->user || pUri->fragment)
                throw Error(ErrorCode::InvalidArgument, "Invalid image authority");
            std::string strScheme = pUri->scheme, strHost = pUri->server;
            std::transform(strScheme.begin(), strScheme.end(), strScheme.begin(),
                           [](unsigned char nC)
                           {
                               return static_cast<char>(std::tolower(nC));
                           });
            std::transform(strHost.begin(), strHost.end(), strHost.begin(),
                           [](unsigned char nC)
                           {
                               return static_cast<char>(std::tolower(nC));
                           });
            if (strScheme != "https" &&
                !(strScheme == "http" &&
                  (strHost == "127.0.0.1" || strHost == "localhost" || strHost == "[::1]")))
                throw Error(ErrorCode::InvalidArgument, "Images require HTTPS");
            auto strResult = strScheme + "://" + strHost;
            if (pUri->port > 0 && !((strScheme == "https" && pUri->port == 443) ||
                                    (strScheme == "http" && pUri->port == 80)))
                strResult += ':' + std::to_string(pUri->port);
            return strResult;
        }
        bool imageBytes(const std::string& strB)
        {
            return (strB.size() >= 3 && static_cast<unsigned char>(strB[0]) == 255 &&
                    static_cast<unsigned char>(strB[1]) == 216 &&
                    static_cast<unsigned char>(strB[2]) == 255) ||
                   (strB.size() >= 8 && strB.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) ||
                   strB.compare(0, 6, "GIF87a") == 0 || strB.compare(0, 6, "GIF89a") == 0 ||
                   (strB.size() >= 12 && strB.compare(0, 4, "RIFF") == 0 &&
                    strB.compare(8, 4, "WEBP") == 0);
        }
        std::string fetchUrl(const std::string& strUrl)
        {
            // Preserve original markers for offline lookup, but fetch legacy official
            // picture links over HTTPS with normal certificate verification.
            for (const auto pPrefix : {"http://pic.wenku8.com/", "http://img.wenku8.com/"})
                if (strUrl.rfind(pPrefix, 0) == 0)
                    return "https://" + strUrl.substr(7);
            return strUrl;
        }
        struct Part
        {
            int m_nId{};
            std::string m_strObject;
        };
        struct Asset
        {
            std::string m_strUrl, m_strObject;
        };
        struct Plan
        {
            int m_nNovelId{};
            DownloadState m_eState = DownloadState::Running;
            bool m_bImages = true, m_bCover = true;
            std::optional<ErrorCode> m_oError;
            Novel m_oNovel;
            std::vector<Volume> m_vVolumes;
            std::vector<Part> m_vChapters;
            std::vector<Asset> m_vAssets;
        };
        tinyxml2::XMLElement* element(tinyxml2::XMLDocument& oDoc, tinyxml2::XMLNode* pParent,
                                      const char* pName)
        {
            auto pE = oDoc.NewElement(pName);
            pParent->InsertEndChild(pE);
            return pE;
        }
        std::string serialize(const Plan& oP, const std::string& strSource)
        {
            tinyxml2::XMLDocument oDoc;
            auto pRoot = element(oDoc, &oDoc, "download");
            pRoot->SetAttribute("version", 1);
            pRoot->SetAttribute("source", strSource.c_str());
            pRoot->SetAttribute("id", oP.m_nNovelId);
            pRoot->SetAttribute("state", static_cast<int>(oP.m_eState));
            pRoot->SetAttribute("images", oP.m_bImages);
            pRoot->SetAttribute("cover", oP.m_bCover);
            if (oP.m_oError)
                pRoot->SetAttribute("error", static_cast<int>(*oP.m_oError));
            auto pMeta = element(oDoc, pRoot, "novel");
            pMeta->SetAttribute("title", oP.m_oNovel.m_strTitle.c_str());
            pMeta->SetAttribute("author", oP.m_oNovel.m_strAuthor.c_str());
            pMeta->SetAttribute("status", oP.m_oNovel.m_strStatus.c_str());
            pMeta->SetAttribute("updated", oP.m_oNovel.m_strLastUpdate.c_str());
            pMeta->SetAttribute("publisher", oP.m_oNovel.m_strPublisher.c_str());
            element(oDoc, pMeta, "intro")->SetText(oP.m_oNovel.m_strIntro.c_str());
            pMeta->SetAttribute("hits", oP.m_oNovel.m_nHits);
            pMeta->SetAttribute("votes", oP.m_oNovel.m_nVotes);
            pMeta->SetAttribute("favorites", oP.m_oNovel.m_nFavorites);
            pMeta->SetAttribute("length", oP.m_oNovel.m_nLength);
            pMeta->SetAttribute("latest-id", oP.m_oNovel.m_nLatestChapterId);
            pMeta->SetAttribute("latest-title", oP.m_oNovel.m_strLatestChapter.c_str());
            auto pVolumes = element(oDoc, pRoot, "volumes");
            for (const auto& oV : oP.m_vVolumes)
            {
                auto pVolume = element(oDoc, pVolumes, "volume");
                pVolume->SetAttribute("id", oV.m_nId);
                pVolume->SetAttribute("title", oV.m_strTitle.c_str());
                for (const auto& oC : oV.m_vChapters)
                {
                    auto pChapter = element(oDoc, pVolume, "chapter");
                    pChapter->SetAttribute("id", oC.m_nId);
                    pChapter->SetAttribute("title", oC.m_strTitle.c_str());
                }
            }
            auto pChapters = element(oDoc, pRoot, "chapters");
            for (const auto& oC : oP.m_vChapters)
            {
                auto pE = element(oDoc, pChapters, "chapter");
                pE->SetAttribute("id", oC.m_nId);
                pE->SetAttribute("object", oC.m_strObject.c_str());
            }
            auto pAssets = element(oDoc, pRoot, "assets");
            for (const auto& oA : oP.m_vAssets)
            {
                auto pE = element(oDoc, pAssets, "image");
                pE->SetAttribute("url", oA.m_strUrl.c_str());
                pE->SetAttribute("object", oA.m_strObject.c_str());
            }
            tinyxml2::XMLPrinter oPrinter;
            oDoc.Print(&oPrinter);
            if (oPrinter.CStrSize() > 8 * 1024 * 1024)
                throw Error(ErrorCode::ResponseTooLarge, "Download manifest exceeds 8 MiB");
            return oPrinter.CStr();
        }
    } // namespace

    struct NovelDownloader::Impl
    {
        ClientOptions m_oSource;
        DownloadStoreOptions m_oStore;
        std::shared_ptr<HttpTransport> m_pContentTransport, m_pAssetTransport;
        std::string m_strSourceKey;
        std::set<std::string> m_vOrigins;
        std::filesystem::path directory(int nNovelId) const
        {
            positive(nNovelId);
            return m_oStore.m_oDirectory / m_strSourceKey / std::to_string(nNovelId);
        }
        std::string object(int nNovelId, const std::string& strName) const
        {
            if (!std::regex_match(strName, std::regex("[0-9a-f]{16}-[0-9]{1,10}\\.bin")))
                throw Error(ErrorCode::Parse, "Invalid download object name");
            auto strBytes = read(directory(nNovelId) / "objects" / strName, nObjectLimit);
            if (objectName(strBytes) != strName)
                throw Error(ErrorCode::CacheMiss, "Download object is corrupt");
            return strBytes;
        }
        bool valid(int nNovelId, const std::string& strName) const
        {
            if (strName.empty())
                return false;
            try
            {
                object(nNovelId, strName);
                return true;
            }
            catch (const Error&)
            {
                return false;
            }
        }
        std::string put(int nNovelId, const std::string& strBytes)
        {
            if (strBytes.size() > nObjectLimit)
                throw Error(ErrorCode::ResponseTooLarge, "Download object exceeds 128 MiB");
            auto strName = objectName(strBytes);
            auto oPath = directory(nNovelId) / "objects" / strName;
            if (valid(nNovelId, strName))
            {
                if (object(nNovelId, strName) != strBytes)
                    throw Error(ErrorCode::Io, "Download object checksum collision");
            }
            else
                Detail::atomicWrite(oPath, strBytes);
            return strName;
        }
        void save(const Plan& oP, const char* pFile)
        {
            Detail::atomicWrite(directory(oP.m_nNovelId) / pFile, serialize(oP, m_strSourceKey));
        }
        Plan load(int nNovelId, const char* pFile) const
        {
            const auto strBytes = read(directory(nNovelId) / pFile, 8 * 1024 * 1024);
            tinyxml2::XMLDocument oDoc;
            if (strBytes.find('\0') != std::string::npos ||
                oDoc.Parse(strBytes.data(), strBytes.size()) != tinyxml2::XML_SUCCESS ||
                !oDoc.RootElement() || oDoc.RootElement()->NextSiblingElement())
                throw Error(ErrorCode::Parse, "Malformed download manifest");
            for (auto pN = oDoc.FirstChild(); pN; pN = pN->NextSibling())
                if (pN->ToUnknown())
                    throw Error(ErrorCode::Parse, "Download manifest DTD rejected");
            auto pRoot = oDoc.RootElement();
            if (std::string(pRoot->Name()) != "download" || attr(pRoot, "version") != "1" ||
                attr(pRoot, "source") != m_strSourceKey || id(attr(pRoot, "id")) != nNovelId)
                throw Error(ErrorCode::Parse, "Download manifest identity mismatch");
            Plan oP;
            oP.m_nNovelId = nNovelId;
            oP.m_oNovel.m_nId = nNovelId;
            auto nState = number(attr(pRoot, "state"));
            if (nState > static_cast<unsigned>(DownloadState::Cancelled))
                throw Error(ErrorCode::Parse, "Invalid download state");
            oP.m_eState = static_cast<DownloadState>(nState);
            if (attr(pRoot, "images") != "true" && attr(pRoot, "images") != "false")
                throw Error(ErrorCode::Parse, "Invalid download flags");
            if (attr(pRoot, "cover") != "true" && attr(pRoot, "cover") != "false")
                throw Error(ErrorCode::Parse, "Invalid download flags");
            oP.m_bImages = attr(pRoot, "images") == "true";
            oP.m_bCover = attr(pRoot, "cover") == "true";
            if (pRoot->Attribute("error"))
            {
                auto nCode = number(attr(pRoot, "error"));
                if (nCode > static_cast<unsigned>(ErrorCode::SiteAccessDenied))
                    throw Error(ErrorCode::Parse, "Invalid download error");
                oP.m_oError = static_cast<ErrorCode>(nCode);
            }
            auto pMeta = pRoot->FirstChildElement("novel"),
                 pVolumes = pRoot->FirstChildElement("volumes"),
                 pChapters = pRoot->FirstChildElement("chapters"),
                 pAssets = pRoot->FirstChildElement("assets");
            if (!pMeta || !pVolumes || !pChapters || !pAssets)
                throw Error(ErrorCode::Parse, "Incomplete download manifest");
            oP.m_oNovel.m_strTitle = attr(pMeta, "title");
            oP.m_oNovel.m_strAuthor = attr(pMeta, "author");
            oP.m_oNovel.m_strStatus = attr(pMeta, "status");
            oP.m_oNovel.m_strLastUpdate = attr(pMeta, "updated");
            oP.m_oNovel.m_strPublisher = attr(pMeta, "publisher");
            const auto fnMetric = [&](const char* pKey)
            {
                return pMeta->Attribute(pKey) ? number(attr(pMeta, pKey)) : 0;
            };
            oP.m_oNovel.m_nHits = fnMetric("hits");
            oP.m_oNovel.m_nVotes = fnMetric("votes");
            oP.m_oNovel.m_nFavorites = fnMetric("favorites");
            oP.m_oNovel.m_nLength = fnMetric("length");
            const auto nLatest = fnMetric("latest-id");
            if (nLatest > 2147483647)
                throw Error(ErrorCode::Parse, "Invalid latest chapter ID");
            oP.m_oNovel.m_nLatestChapterId = static_cast<int>(nLatest);
            oP.m_oNovel.m_strLatestChapter = attr(pMeta, "latest-title");
            auto pIntro = pMeta->FirstChildElement("intro");
            if (pIntro && pIntro->GetText())
                oP.m_oNovel.m_strIntro = pIntro->GetText();
            std::set<int> vIds;
            for (auto pV = pVolumes->FirstChildElement("volume"); pV;
                 pV = pV->NextSiblingElement("volume"))
            {
                Volume oVolume;
                oVolume.m_nId = id(attr(pV, "id"));
                oVolume.m_strTitle = attr(pV, "title");
                for (auto pC = pV->FirstChildElement("chapter"); pC;
                     pC = pC->NextSiblingElement("chapter"))
                {
                    auto nChapterId = id(attr(pC, "id"));
                    if (!vIds.insert(nChapterId).second)
                        throw Error(ErrorCode::Parse, "Duplicate download chapter");
                    oVolume.m_vChapters.push_back({nChapterId, attr(pC, "title")});
                }
                oP.m_vVolumes.push_back(std::move(oVolume));
            }
            std::set<int> vParts;
            for (auto pC = pChapters->FirstChildElement("chapter"); pC;
                 pC = pC->NextSiblingElement("chapter"))
            {
                auto nChapterId = id(attr(pC, "id"));
                if (!vIds.count(nChapterId) || !vParts.insert(nChapterId).second)
                    throw Error(ErrorCode::Parse, "Unexpected download chapter");
                oP.m_vChapters.push_back({nChapterId, attr(pC, "object")});
            }
            if (vParts != vIds || (vIds.empty() && oP.m_eState == DownloadState::Complete))
                throw Error(ErrorCode::Parse, "Incomplete chapter plan");
            std::set<std::string> vUrls;
            for (auto pA = pAssets->FirstChildElement("image"); pA;
                 pA = pA->NextSiblingElement("image"))
            {
                auto strUrl = attr(pA, "url");
                origin(fetchUrl(strUrl));
                if (!vUrls.insert(strUrl).second)
                    throw Error(ErrorCode::Parse, "Duplicate download image");
                oP.m_vAssets.push_back({strUrl, attr(pA, "object")});
            }
            return oP;
        }
        void allowed(const std::string& strUrl) const
        {
            if (!m_vOrigins.count(origin(fetchUrl(strUrl))))
                throw Error(ErrorCode::InvalidArgument, "Image origin is not allowed");
        }
        void discover(Plan& oP, const std::string& strChapter) const
        {
            if (!oP.m_bImages)
                return;
            const std::string strMarker = "<!--image-->";
            for (std::size_t nAt = 0; (nAt = strChapter.find(strMarker, nAt)) != std::string::npos;)
            {
                auto nEnd = strChapter.find(strMarker, nAt + strMarker.size());
                if (nEnd == std::string::npos)
                    throw Error(ErrorCode::Parse, "Unclosed chapter image marker");
                auto strUrl =
                    strChapter.substr(nAt + strMarker.size(), nEnd - nAt - strMarker.size());
                allowed(strUrl);
                if (std::none_of(oP.m_vAssets.begin(), oP.m_vAssets.end(),
                                 [&](const Asset& oA)
                                 {
                                     return oA.m_strUrl == strUrl;
                                 }))
                    oP.m_vAssets.push_back({strUrl, {}});
                nAt = nEnd + strMarker.size();
            }
        }
        DownloadStatus describe(const Plan& oP) const
        {
            DownloadStatus oS;
            oS.m_nNovelId = oP.m_nNovelId;
            oS.m_eState = oP.m_eState;
            oS.m_oLastError = oP.m_oError;
            oS.m_nChaptersTotal = oP.m_vChapters.size();
            oS.m_nImagesTotal = oP.m_vAssets.size();
            for (const auto& oC : oP.m_vChapters)
                oS.m_nChaptersCompleted += valid(oP.m_nNovelId, oC.m_strObject);
            for (const auto& oA : oP.m_vAssets)
                oS.m_nImagesCompleted += valid(oP.m_nNovelId, oA.m_strObject);
            if (oS.m_eState == DownloadState::Complete &&
                (oS.m_nChaptersCompleted != oS.m_nChaptersTotal ||
                 oS.m_nImagesCompleted != oS.m_nImagesTotal))
            {
                oS.m_eState = DownloadState::Failed;
                oS.m_oLastError = ErrorCode::CacheMiss;
            }
            return oS;
        }
        Plan committed(int nNovelId) const
        {
            auto oP = load(nNovelId, "book.xml");
            auto oS = describe(oP);
            if (oS.m_eState != DownloadState::Complete)
                throw Error(ErrorCode::CacheMiss, "No complete readable download");
            return oP;
        }
        bool readable(int nNovelId) const
        {
            try
            {
                committed(nNovelId);
                return true;
            }
            catch (const Error&)
            {
                return false;
            }
        }
    };

    NovelDownloader::NovelDownloader(ClientOptions oSource, DownloadStoreOptions oStore,
                                     std::shared_ptr<HttpTransport> pContent,
                                     std::shared_ptr<HttpTransport> pAssets)
        : m_pImpl(new Impl)
    {
        if (oStore.m_oDirectory.empty() || !oStore.m_oAssets.m_nMaxResponseBytes ||
            !oSource.m_oTransfer.m_nMaxResponseBytes ||
            oStore.m_oAssets.m_nMaxResponseBytes > nObjectLimit ||
            oSource.m_oTransfer.m_nMaxResponseBytes > nObjectLimit ||
            oStore.m_oAssets.m_oTimeout.count() <= 0 ||
            oStore.m_oAssets.m_oConnectTimeout.count() <= 0 ||
            oStore.m_oRequestInterval.count() < 0 ||
            oStore.m_oRequestInterval > std::chrono::seconds(60))
            throw Error(ErrorCode::InvalidArgument, "Invalid download store or object limit");
        Client oValidate(oSource, pContent);
        m_pImpl->m_strSourceKey =
            hash(formEncode({{"endpoint", oSource.m_strEndpoint},
                             {"protocol", std::to_string(static_cast<int>(oSource.m_eProtocol))},
                             {"language", std::to_string(static_cast<int>(oSource.m_eLanguage))},
                             {"appver", oSource.m_strLegacyAppVersion}}));
        for (const auto& strUrl : oStore.m_vImageOrigins)
        {
            if (origin(strUrl) != strUrl)
                throw Error(ErrorCode::InvalidArgument,
                            "Image allowlist must contain canonical origins");
            m_pImpl->m_vOrigins.insert(strUrl);
        }
        m_pImpl->m_oSource = std::move(oSource);
        m_pImpl->m_oStore = std::move(oStore);
        m_pImpl->m_pContentTransport = std::move(pContent);
        m_pImpl->m_pAssetTransport = std::move(pAssets);
    }
    NovelDownloader::~NovelDownloader() = default;
    NovelDownloader::NovelDownloader(NovelDownloader&&) noexcept = default;
    NovelDownloader& NovelDownloader::operator=(NovelDownloader&&) noexcept = default;
    std::filesystem::path NovelDownloader::bookDirectory(int nNovelId) const
    {
        return m_pImpl->directory(nNovelId);
    }
    DownloadStatus NovelDownloader::status(int nNovelId) const
    {
        positive(nNovelId);
        DownloadStatus oS;
        oS.m_nNovelId = nNovelId;
        try
        {
            oS = m_pImpl->describe(m_pImpl->load(nNovelId, "task.xml"));
        }
        catch (const Error& oE)
        {
            if (oE.code() != ErrorCode::CacheMiss)
            {
                oS.m_eState = DownloadState::Failed;
                oS.m_oLastError = oE.code();
            }
        }
        oS.m_bHasReadableVersion = m_pImpl->readable(nNovelId);
        if (oS.m_eState == DownloadState::Complete)
        {
            bool bPublished = false;
            try
            {
                bPublished =
                    serialize(m_pImpl->load(nNovelId, "task.xml"), m_pImpl->m_strSourceKey) ==
                    serialize(m_pImpl->load(nNovelId, "book.xml"), m_pImpl->m_strSourceKey);
            }
            catch (const Error&)
            {
            }
            if (!bPublished || !oS.m_bHasReadableVersion)
            {
                oS.m_eState = DownloadState::Failed;
                oS.m_oLastError = ErrorCode::CacheMiss;
            }
        }
        return oS;
    }
    DownloadStatus NovelDownloader::download(int nNovelId, const DownloadRequest& oRequest)
    {
        positive(nNovelId);
        if (oRequest.m_nRateLimitRetries > 5 || oRequest.m_oRateLimitWait.count() < 0 ||
            oRequest.m_oRateLimitWait > std::chrono::hours(1))
            throw Error(ErrorCode::InvalidArgument, "Invalid download retry limits");
        if (oRequest.m_eMode != DownloadMode::Resume && oRequest.m_eMode != DownloadMode::Refresh &&
            oRequest.m_eMode != DownloadMode::Offline)
            throw Error(ErrorCode::InvalidArgument, "Invalid download mode");
        if (oRequest.m_eSourcePolicy != CachePolicy::PreferCache &&
            oRequest.m_eSourcePolicy != CachePolicy::CacheOnly &&
            oRequest.m_eSourcePolicy != CachePolicy::Refresh &&
            oRequest.m_eSourcePolicy != CachePolicy::NetworkOnly)
            throw Error(ErrorCode::InvalidArgument, "Invalid download source policy");
        if (oRequest.m_eMode == DownloadMode::Offline)
        {
            auto oP = m_pImpl->committed(nNovelId);
            if ((oRequest.m_bIncludeImages && !oP.m_bImages) ||
                (oRequest.m_bIncludeCover && !oP.m_bCover))
                throw Error(ErrorCode::CacheMiss, "Offline version lacks requested assets");
            auto oS = m_pImpl->describe(oP);
            oS.m_bHasReadableVersion = true;
            return oS;
        }
        Plan oP;
        oP.m_nNovelId = nNovelId;
        oP.m_oNovel.m_nId = nNovelId;
        oP.m_bImages = oRequest.m_bIncludeImages;
        oP.m_bCover = oRequest.m_bIncludeCover;
        if (oRequest.m_eMode == DownloadMode::Resume)
        {
            try
            {
                auto oOld = m_pImpl->load(nNovelId, "task.xml");
                if (oOld.m_bImages == oP.m_bImages && oOld.m_bCover == oP.m_bCover)
                    oP = std::move(oOld);
            }
            catch (const Error& oE)
            {
                if (oE.code() != ErrorCode::CacheMiss)
                    throw;
            }
        }
        oP.m_eState = DownloadState::Running;
        oP.m_oError.reset();
        DownloadStatus oCurrent;
        oCurrent.m_nNovelId = nNovelId;
        oCurrent.m_eState = DownloadState::Running;
        oCurrent.m_bHasReadableVersion = m_pImpl->readable(nNovelId);
        auto fnNotify = [&]
        {
            if (!oRequest.m_fnProgress)
                return;
            bool bProceed;
            try
            {
                bProceed = oRequest.m_fnProgress(oCurrent);
            }
            catch (...)
            {
                throw Error(ErrorCode::Network, "Download progress callback failed");
            }
            if (!bProceed)
                throw Error(ErrorCode::Cancelled, "Novel download cancelled");
        };
        auto fnCheckpoint = [&]
        {
            oCurrent.m_eState = oP.m_eState;
            oCurrent.m_oLastError = oP.m_oError;
            oCurrent.m_nChaptersTotal = oP.m_vChapters.size();
            oCurrent.m_nImagesTotal = oP.m_vAssets.size();
            m_pImpl->save(oP, "task.xml");
            fnNotify();
        };
        std::exception_ptr oProgressFailure;
        try
        {
            fnNotify();
            auto oSource = m_pImpl->m_oSource;
            auto fnOriginalProgress = oSource.m_oTransfer.m_fnProgress;
            auto fnTransferProgress = [&](const auto& oOriginal, auto oDone, auto oTotal)
            {
                try
                {
                    fnNotify();
                    if (!oOriginal || oOriginal(oDone, oTotal))
                        return true;
                    oProgressFailure = std::make_exception_ptr(
                        Error(ErrorCode::Cancelled, "Novel transfer cancelled"));
                }
                catch (const Error&)
                {
                    oProgressFailure = std::current_exception();
                }
                catch (...)
                {
                    oProgressFailure = std::make_exception_ptr(
                        Error(ErrorCode::Network, "Download progress callback failed"));
                }
                return false;
            };
            oSource.m_oTransfer.m_fnProgress = [&](auto oDone, auto oTotal)
            {
                return fnTransferProgress(fnOriginalProgress, oDone, oTotal);
            };
            auto pGate = std::make_shared<RequestGate>();
            pGate->m_oInterval = m_pImpl->m_oStore.m_oRequestInterval;
            auto pContent =
                m_pImpl->m_pContentTransport ? m_pImpl->m_pContentTransport : makeCurlTransport();
            Client oClient(oSource, std::make_shared<TaskTransport>(pContent, pGate, oRequest));
            const auto eSourcePolicy = oRequest.m_eMode == DownloadMode::Refresh &&
                                               oRequest.m_eSourcePolicy == CachePolicy::PreferCache
                                           ? CachePolicy::Refresh
                                           : oRequest.m_eSourcePolicy;
            if (oP.m_vChapters.empty())
            {
                oP.m_oNovel = oClient.novel(nNovelId, eSourcePolicy);
                oP.m_oNovel.m_strIntro = oClient.introduction(nNovelId, eSourcePolicy);
                oP.m_vVolumes = oClient.index(nNovelId, eSourcePolicy);
                std::set<int> vIds;
                for (const auto& oV : oP.m_vVolumes)
                {
                    positive(oV.m_nId);
                    for (const auto& oC : oV.m_vChapters)
                    {
                        if (oC.m_nId <= 0 || !vIds.insert(oC.m_nId).second)
                            throw Error(ErrorCode::Parse, "Invalid or duplicate chapter plan");
                        oP.m_vChapters.push_back({oC.m_nId, {}});
                    }
                }
                if (oP.m_vChapters.empty())
                    throw Error(ErrorCode::Parse, "Empty novel download plan");
                if (oP.m_bCover)
                {
                    auto strUrl = "https://img.wenku8.com/image/" +
                                  std::to_string(nNovelId / 1000) + '/' + std::to_string(nNovelId) +
                                  '/' + std::to_string(nNovelId) + "s.jpg";
                    m_pImpl->allowed(strUrl);
                    oP.m_vAssets.push_back({strUrl, {}});
                }
            }
            {
                const auto bReadable = oCurrent.m_bHasReadableVersion;
                oCurrent = m_pImpl->describe(oP);
                oCurrent.m_bHasReadableVersion = bReadable;
            }
            fnCheckpoint();
            for (auto& oPart : oP.m_vChapters)
            {
                if (!m_pImpl->valid(nNovelId, oPart.m_strObject))
                {
                    auto strBytes = oClient.chapter(nNovelId, oPart.m_nId, eSourcePolicy);
                    text(strBytes);
                    m_pImpl->discover(oP, strBytes);
                    oPart.m_strObject = m_pImpl->put(nNovelId, strBytes);
                    ++oCurrent.m_nChaptersCompleted;
                    fnCheckpoint();
                }
                else
                    m_pImpl->discover(oP, m_pImpl->object(nNovelId, oPart.m_strObject));
            }
            // Rebuild references from the final chapter set; repaired chapters may
            // have changed their image links. Keep already validated matching assets.
            auto vPreviousAssets = std::move(oP.m_vAssets);
            oP.m_vAssets.clear();
            if (oP.m_bCover)
                oP.m_vAssets.push_back(
                    {"https://img.wenku8.com/image/" + std::to_string(nNovelId / 1000) + '/' +
                         std::to_string(nNovelId) + '/' + std::to_string(nNovelId) + "s.jpg",
                     {}});
            for (const auto& oPart : oP.m_vChapters)
                m_pImpl->discover(oP, m_pImpl->object(nNovelId, oPart.m_strObject));
            oCurrent.m_nImagesCompleted = 0;
            for (auto& oAsset : oP.m_vAssets)
            {
                auto oOld = std::find_if(vPreviousAssets.begin(), vPreviousAssets.end(),
                                         [&](const Asset& oCandidate)
                                         {
                                             return oCandidate.m_strUrl == oAsset.m_strUrl;
                                         });
                if (oOld != vPreviousAssets.end())
                    oAsset.m_strObject = oOld->m_strObject;
                oCurrent.m_nImagesCompleted += m_pImpl->valid(nNovelId, oAsset.m_strObject);
            }
            fnCheckpoint();
            auto oAssets = m_pImpl->m_oStore.m_oAssets;
            auto fnOriginalAssetProgress = oAssets.m_fnProgress;
            oAssets.m_fnProgress = [&](auto oDone, auto oTotal)
            {
                return fnTransferProgress(fnOriginalAssetProgress, oDone, oTotal);
            };
            oAssets.m_bFollowRedirects = false;
            std::shared_ptr<HttpTransport> pAssetFetcher;
            for (auto& oA : oP.m_vAssets)
            {
                if (m_pImpl->valid(nNovelId, oA.m_strObject))
                    continue;
                auto strUrl = fetchUrl(oA.m_strUrl);
                if (!m_pImpl->m_pAssetTransport)
                    m_pImpl->m_pAssetTransport = makeCurlTransport();
                if (!pAssetFetcher)
                    pAssetFetcher = std::make_shared<TaskTransport>(m_pImpl->m_pAssetTransport,
                                                                    pGate, oRequest);
                HttpResponse oResponse;
                for (unsigned nHop = 0;; ++nHop)
                {
                    m_pImpl->allowed(strUrl);
                    oResponse = pAssetFetcher->perform({strUrl, "GET", {}, {}}, oAssets);
                    if (oProgressFailure)
                        std::rethrow_exception(oProgressFailure);
                    if (oResponse.m_nStatus < 300 || oResponse.m_nStatus >= 400)
                        break;
                    std::string strLocation;
                    for (const auto& oH : oResponse.m_vHeaders)
                    {
                        auto strName = oH.first;
                        std::transform(strName.begin(), strName.end(), strName.begin(),
                                       [](unsigned char nC)
                                       {
                                           return static_cast<char>(std::tolower(nC));
                                       });
                        if (strName == "location")
                            strLocation = oH.second;
                    }
                    if (strLocation.empty() || nHop >= 5)
                        throw Error(ErrorCode::Http, "Invalid image redirect", oResponse.m_nStatus);
                    auto pResolved =
                        xmlBuildURI(BAD_CAST strLocation.c_str(), BAD_CAST strUrl.c_str());
                    if (!pResolved)
                        throw Error(ErrorCode::Parse, "Invalid image redirect URL");
                    strUrl = reinterpret_cast<const char*>(pResolved);
                    xmlFree(pResolved);
                }
                if (oResponse.m_nStatus != 200)
                    throw Error(ErrorCode::Http, "Image request failed", oResponse.m_nStatus);
                if (!oResponse.m_strEffectiveUrl.empty())
                    m_pImpl->allowed(oResponse.m_strEffectiveUrl);
                if (!imageBytes(oResponse.m_strBody))
                    throw Error(ErrorCode::Parse,
                                "Expected image bytes, received an error page or unknown format");
                oA.m_strObject = m_pImpl->put(nNovelId, oResponse.m_strBody);
                ++oCurrent.m_nImagesCompleted;
                fnCheckpoint();
            }
            oP.m_eState = DownloadState::Complete;
            oCurrent = m_pImpl->describe(oP);
            if (oCurrent.m_eState != DownloadState::Complete)
                throw Error(ErrorCode::CacheMiss, "Download files changed before publication");
            m_pImpl->save(oP, "task.xml");
            m_pImpl->save(oP, "book.xml");
            oCurrent.m_bHasReadableVersion = true;
            return oCurrent;
        }
        catch (...)
        {
            auto oFailure = oProgressFailure ? oProgressFailure : std::current_exception();
            try
            {
                std::rethrow_exception(oFailure);
            }
            catch (const Error& oE)
            {
                oP.m_oError = oE.code();
                oP.m_eState = oE.code() == ErrorCode::Cancelled ? DownloadState::Cancelled
                                                                : DownloadState::Failed;
            }
            catch (...)
            {
                oP.m_oError = ErrorCode::Io;
                oP.m_eState = DownloadState::Failed;
            }
            // A failed refresh never replaces the previously committed book.xml.
            try
            {
                m_pImpl->save(oP, "task.xml");
            }
            catch (...)
            {
            }
            std::rethrow_exception(oFailure);
        }
    }
    Novel NovelDownloader::novel(int nNovelId) const
    {
        return m_pImpl->committed(nNovelId).m_oNovel;
    }
    std::vector<Volume> NovelDownloader::index(int nNovelId) const
    {
        return m_pImpl->committed(nNovelId).m_vVolumes;
    }
    std::string NovelDownloader::chapter(int nNovelId, int nChapterId) const
    {
        positive(nChapterId);
        auto oP = m_pImpl->committed(nNovelId);
        for (const auto& oPart : oP.m_vChapters)
            if (oPart.m_nId == nChapterId)
                return m_pImpl->object(nNovelId, oPart.m_strObject);
        throw Error(ErrorCode::CacheMiss, "Chapter is not in the committed download");
    }
    std::vector<std::string> NovelDownloader::imageUrls(int nNovelId) const
    {
        auto oP = m_pImpl->committed(nNovelId);
        std::vector<std::string> vUrls;
        for (const auto& oA : oP.m_vAssets)
            vUrls.push_back(oA.m_strUrl);
        return vUrls;
    }
    std::string NovelDownloader::image(int nNovelId, const std::string& strUrl) const
    {
        auto oP = m_pImpl->committed(nNovelId);
        for (const auto& oA : oP.m_vAssets)
            if (oA.m_strUrl == strUrl)
                return m_pImpl->object(nNovelId, oA.m_strObject);
        throw Error(ErrorCode::CacheMiss, "Image is not in the committed download");
    }
    void NovelDownloader::visitContent(int nNovelId, const DownloadContentVisitor& oVisitor) const
    {
        positive(nNovelId);
        const auto oPlan = m_pImpl->load(nNovelId, "book.xml");
        if (oPlan.m_eState != DownloadState::Complete)
        {
            throw Error(ErrorCode::CacheMiss, "No complete readable download");
        }
        std::vector<std::string> vImages;
        for (const auto& oAsset : oPlan.m_vAssets)
        {
            vImages.push_back(oAsset.m_strUrl);
        }
        if (oVisitor.m_fnBook)
        {
            oVisitor.m_fnBook(oPlan.m_oNovel, oPlan.m_vVolumes, vImages);
        }
        if (oVisitor.m_fnImage)
        {
            for (const auto& oAsset : oPlan.m_vAssets)
            {
                oVisitor.m_fnImage(oAsset.m_strUrl,
                                   m_pImpl->object(nNovelId, oAsset.m_strObject));
            }
        }
        if (oVisitor.m_fnChapter)
        {
            std::map<int, std::string> vObjects;
            for (const auto& oPart : oPlan.m_vChapters)
            {
                vObjects.emplace(oPart.m_nId, oPart.m_strObject);
            }
            for (const auto& oVolume : oPlan.m_vVolumes)
            {
                for (const auto& oChapter : oVolume.m_vChapters)
                {
                    oVisitor.m_fnChapter(oChapter.m_nId,
                                         m_pImpl->object(nNovelId, vObjects.at(oChapter.m_nId)));
                }
            }
        }
    }
} // namespace Wenku8
