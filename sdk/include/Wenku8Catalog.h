// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"

namespace Wenku8
{
    struct CatalogLink
    {
        std::string m_strTitle, m_strUrl, m_strMediaType, m_strRelation;
    };
    struct CatalogBook
    {
        Novel m_oNovel; // Fields supplied by the feed only; counters/status may be absent.
        std::string m_strEntryId;
        std::vector<CatalogLink>
            m_vLinks; // Cover/acquisition URLs are returned, never fetched implicitly.
    };
    struct CatalogPage
    {
        std::string m_strTitle, m_strUrl, m_strNextUrl, m_strPreviousUrl, m_strSearchDescriptionUrl;
        std::vector<CatalogLink> m_vCollections;
        std::vector<CatalogBook> m_vBooks;
    };
    struct CatalogOptions
    {
        // Explicit third-party source, e.g. https://opds.wol.moe/zh_CN.
        // There is no default source or hidden fallback from the Wenku8 account client.
        std::string m_strEndpoint;
        std::filesystem::path m_oCacheDirectory;
        std::chrono::seconds m_oCacheMaxAge{3600};
        TransferOptions m_oTransfer;
    };
    // Read-only OPDS/Atom client. No account/password interface or browser dependency.
    // Synchronous; do not call one instance concurrently.
    class WENKU8_API CatalogClient
    {
      public:
        explicit CatalogClient(CatalogOptions, std::shared_ptr<HttpTransport> pTransport = {});
        ~CatalogClient();
        CatalogClient(CatalogClient&&) noexcept;
        CatalogClient& operator=(CatalogClient&&) noexcept;
        CatalogClient(const CatalogClient&) = delete;
        CatalogClient& operator=(const CatalogClient&) = delete;
        CatalogPage browse(const std::string& strUrl = {}, CachePolicy = CachePolicy::PreferCache);
        // Follows a declared sort collection; no page-count/URL guessing.
        CatalogPage list(Sort, CachePolicy = CachePolicy::PreferCache);
        CatalogPage search(const std::string&, CachePolicy = CachePolicy::PreferCache);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
} // namespace Wenku8
