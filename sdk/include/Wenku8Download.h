// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8
{
    enum class DownloadState
    {
        NotStarted,
        Running,
        Complete,
        Failed,
        Cancelled
    };
    enum class DownloadMode
    {
        // Reuse the last task checkpoint with matching image/cover options.
        // Completed objects are verified and skipped; an interrupted HTTP
        // response is downloaded again from the start of that chapter/image.
        Resume,
        Refresh,
        Offline
    };
    struct DownloadStatus
    {
        int m_nNovelId = 0;
        DownloadState m_eState = DownloadState::NotStarted;
        std::size_t m_nChaptersTotal = 0, m_nChaptersCompleted = 0;
        std::size_t m_nImagesTotal = 0, m_nImagesCompleted = 0;
        bool m_bHasReadableVersion = false;
        std::optional<ErrorCode> m_oLastError;
    };
    struct DownloadRequest
    {
        DownloadMode m_eMode = DownloadMode::Resume;
        CachePolicy m_eSourcePolicy = CachePolicy::PreferCache;
        bool m_bIncludeImages = true;
        bool m_bIncludeCover = true;
        unsigned m_nRateLimitRetries = 1; // GET/429 only; other errors remain visible.
        std::chrono::seconds m_oRateLimitWait{
            60}; // Retry-After upper bound; also fallback if absent.
        // Called on the downloading thread, including while transfers run.
        // Completed chapter/image counts are checkpointed before notification;
        // a restarted Resume reports those counts from its first callback.
        // False cancels; callbacks must not re-enter this downloader.
        std::function<bool(const DownloadStatus&)> m_fnProgress;
    };
    struct DownloadStoreOptions
    {
        std::filesystem::path m_oDirectory;
        TransferOptions m_oAssets;
        std::chrono::milliseconds m_oRequestInterval{
            2000}; // Minimum time between HTTP request starts.
        // Explicit origin allowlist; no cookies/headers are copied from content requests.
        // HTTP is permitted only for explicitly configured loopback test origins.
        std::vector<std::string> m_vImageOrigins{"https://img.wenku8.com",
                                                 "https://pic.wenku8.com"};
    };
    struct DownloadContentVisitor
    {
        std::function<void(const Novel&, const std::vector<Volume>&,
                           const std::vector<std::string>&)> m_fnBook;
        std::function<void(int, const std::string&)> m_fnChapter;
        std::function<void(const std::string&, const std::string&)> m_fnImage;
    };
    struct ExportProgress
    {
        std::size_t m_nChaptersCompleted = 0, m_nChaptersTotal = 0;
        std::size_t m_nImagesCompleted = 0, m_nImagesTotal = 0;
    };
    struct ExportOptions
    {
        std::filesystem::path m_oDirectory;
        bool m_bIncludeImages = true;
        // False cancels the export. The callback runs on the calling thread.
        std::function<bool(const ExportProgress&)> m_fnProgress;
    };
    struct ExportResult
    {
        std::filesystem::path m_oDirectory, m_oTextFile, m_oHtmlFile;
        std::size_t m_nChapters = 0, m_nImages = 0;
    };
    // Pure C++ persisted download store. One instance/novel directory at a time.
    // Separate content and asset transports: a browser session can fetch protected
    // text while a dedicated curl session fetches public image nodes.
    class WENKU8_API NovelDownloader
    {
      public:
        NovelDownloader(ClientOptions, DownloadStoreOptions,
                        std::shared_ptr<HttpTransport> pContentTransport = {},
                        std::shared_ptr<HttpTransport> pAssetTransport = {});
        ~NovelDownloader();
        NovelDownloader(NovelDownloader&&) noexcept;
        NovelDownloader& operator=(NovelDownloader&&) noexcept;
        NovelDownloader(const NovelDownloader&) = delete;
        NovelDownloader& operator=(const NovelDownloader&) = delete;
        DownloadStatus download(int nNovelId, const DownloadRequest& = {});
        DownloadStatus status(int nNovelId) const; // Checks persisted files; never performs HTTP.
        // Metadata comes from the published Complete manifest, without scanning
        // every content object. chapter()/image() validate the requested object;
        // use status() for an explicit full-book integrity check.
        Novel novel(int nNovelId) const;
        std::vector<Volume> index(int nNovelId) const;
        std::string chapter(int nNovelId, int nChapterId) const;
        std::vector<std::string> imageUrls(int nNovelId) const;
        std::string image(int nNovelId, const std::string& strUrl) const;
        std::filesystem::path bookDirectory(int nNovelId) const;
        // Offline streaming: metadata, requested images, then ordered chapters.
        // Each content object is validated when read; a later failure can follow
        // earlier callbacks. Omit m_fnImage to read/export text without images.
        void visitContent(int nNovelId, const DownloadContentVisitor&) const;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
    // Exports a committed cache into a new, uniquely named book directory.
    // Produces UTF-8 novel.txt, index.html and optional local images; no HTTP,
    // cookies, cache manifests or source URLs are copied into the public files.
    WENKU8_API ExportResult exportNovel(const NovelDownloader&, int nNovelId,
                                       const ExportOptions&);
} // namespace Wenku8
