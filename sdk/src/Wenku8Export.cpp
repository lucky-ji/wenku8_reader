// SPDX-License-Identifier: GPL-2.0-only
#include "Wenku8Download.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <system_error>

namespace Wenku8
{
    namespace
    {
        std::string uniqueToken()
        {
            static std::atomic<std::uint64_t> nSequence{0};
            std::random_device oRandom;
            const auto nClock = std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
            const auto nValue = (std::uint64_t(oRandom()) << 32) ^ oRandom() ^ nClock ^ ++nSequence;
            std::ostringstream oText;
            oText << std::hex << std::setw(16) << std::setfill('0') << nValue;
            return oText.str();
        }
        std::string safeTitle(const std::string& strTitle)
        {
            std::string strSafe;
            for (const auto cValue : strTitle)
            {
                const auto nValue = static_cast<unsigned char>(cValue);
                if (nValue < 32 || nValue == 127 || std::string("<>:\"/\\|?*").find(cValue) != std::string::npos)
                {
                    strSafe += '_';
                }
                else
                {
                    strSafe += cValue;
                }
            }
            while (!strSafe.empty() && (strSafe.front() == ' ' || strSafe.front() == '.'))
            {
                strSafe.erase(strSafe.begin());
            }
            while (!strSafe.empty() && (strSafe.back() == ' ' || strSafe.back() == '.'))
            {
                strSafe.pop_back();
            }
            if (strSafe.size() > 96)
            {
                std::size_t nEnd = 96;
                while (nEnd && (static_cast<unsigned char>(strSafe[nEnd]) & 0xc0) == 0x80)
                {
                    --nEnd;
                }
                strSafe.resize(nEnd);
            }
            std::string strDevice = strSafe.substr(0, strSafe.find('.'));
            std::transform(strDevice.begin(), strDevice.end(), strDevice.begin(),
                           [](unsigned char nValue)
                           {
                               return static_cast<char>(std::toupper(nValue));
                           });
            const bool bReserved = strDevice == "CON" || strDevice == "PRN" || strDevice == "AUX" ||
                                   strDevice == "NUL" ||
                                   (strDevice.size() == 4 && strDevice[3] >= '1' && strDevice[3] <= '9' &&
                                    (strDevice.substr(0, 3) == "COM" || strDevice.substr(0, 3) == "LPT"));
            if (strSafe.empty())
            {
                strSafe = "novel";
            }
            else if (bReserved)
            {
                strSafe = "novel-" + strSafe;
            }
            return strSafe;
        }
        std::string htmlText(const std::string& strText)
        {
            std::string strEscaped;
            for (const auto cValue : strText)
            {
                switch (cValue)
                {
                case '&': strEscaped += "&amp;"; break;
                case '<': strEscaped += "&lt;"; break;
                case '>': strEscaped += "&gt;"; break;
                case '\"': strEscaped += "&quot;"; break;
                case '\'': strEscaped += "&#39;"; break;
                case '\r': break;
                case '\n': strEscaped += "<br>\n"; break;
                default: strEscaped += cValue; break;
                }
            }
            return strEscaped;
        }
        void writeBytes(const std::filesystem::path& oPath, const std::string& strBytes)
        {
            std::ofstream oFile(oPath, std::ios::binary | std::ios::trunc);
            if (!oFile || !oFile.write(strBytes.data(), static_cast<std::streamsize>(strBytes.size())))
            {
                throw Error(ErrorCode::Io, "Cannot write exported file");
            }
            oFile.close();
            if (!oFile)
            {
                throw Error(ErrorCode::Io, "Cannot finish exported file");
            }
        }
        std::string imageExtension(const std::string& strBytes)
        {
            if (strBytes.size() >= 3 && static_cast<unsigned char>(strBytes[0]) == 255 &&
                static_cast<unsigned char>(strBytes[1]) == 216 && static_cast<unsigned char>(strBytes[2]) == 255)
            {
                return ".jpg";
            }
            if (strBytes.size() >= 8 && strBytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0)
            {
                return ".png";
            }
            if (strBytes.compare(0, 6, "GIF87a") == 0 || strBytes.compare(0, 6, "GIF89a") == 0)
            {
                return ".gif";
            }
            if (strBytes.size() >= 12 && strBytes.compare(0, 4, "RIFF") == 0 &&
                strBytes.compare(8, 4, "WEBP") == 0)
            {
                return ".webp";
            }
            throw Error(ErrorCode::Parse, "Cached illustration has an unsupported image format");
        }
        class ExportStaging final
        {
          public:
            std::filesystem::path m_oPath;
            explicit ExportStaging(const std::filesystem::path& oRoot)
            {
                for (int nAttempt = 0; nAttempt < 32; ++nAttempt)
                {
                    m_oPath = oRoot / (".wenku8-export-" + uniqueToken());
                    if (std::filesystem::create_directory(m_oPath))
                    {
                        return;
                    }
                }
                throw Error(ErrorCode::Io, "Cannot create an export staging directory");
            }
            ~ExportStaging()
            {
                if (!m_oPath.empty())
                {
                    std::error_code oError;
                    std::filesystem::remove_all(m_oPath, oError);
                }
            }
        };
        struct ChapterHeading
        {
            std::string m_strVolume, m_strTitle;
            bool m_bVolumeStart = false;
        };
        void formatText(const std::string& strText, bool bImages,
                        const std::map<std::string, std::string>& vImages,
                        std::ostream& oText, std::ostream& oHtml)
        {
            const std::string strMarker = "<!--image-->";
            std::size_t nCursor = 0;
            while (nCursor < strText.size())
            {
                const auto nBegin = strText.find(strMarker, nCursor);
                const auto nLength = nBegin == std::string::npos ? strText.size() - nCursor : nBegin - nCursor;
                const auto strPart = strText.substr(nCursor, nLength);
                oText << strPart;
                oHtml << htmlText(strPart);
                if (nBegin == std::string::npos)
                {
                    break;
                }
                const auto nEnd = strText.find(strMarker, nBegin + strMarker.size());
                if (nEnd == std::string::npos)
                {
                    throw Error(ErrorCode::Parse, "Unclosed cached illustration marker");
                }
                const auto strUrl = strText.substr(nBegin + strMarker.size(), nEnd - nBegin - strMarker.size());
                if (bImages)
                {
                    const auto oImage = vImages.find(strUrl);
                    if (oImage == vImages.end())
                    {
                        throw Error(ErrorCode::CacheMiss, "A chapter illustration is missing from the download");
                    }
                    oText << "\n[插图：" << oImage->second << "]\n";
                    oHtml << "</p><figure><img loading=\"lazy\" src=\"" << oImage->second
                          << "\" alt=\"小说插图\"></figure><p class=\"chapter-text\">";
                }
                else
                {
                    oText << "\n[插图未包含]\n";
                    oHtml << "<span class=\"image-note\">[插图未包含]</span>";
                }
                nCursor = nEnd + strMarker.size();
            }
        }
    }
    ExportResult exportNovel(const NovelDownloader& oSource, int nNovelId,
                              const ExportOptions& oOptions)
    {
        if (nNovelId <= 0 || oOptions.m_oDirectory.empty())
        {
            throw Error(ErrorCode::InvalidArgument, "An export directory and positive novel ID are required");
        }
        ExportProgress oProgress;
        auto fnProgress = [&]()
        {
            if (oOptions.m_fnProgress && !oOptions.m_fnProgress(oProgress))
            {
                throw Error(ErrorCode::Cancelled, "Novel export cancelled");
            }
        };
        fnProgress();
        try
        {
            const auto oRoot = std::filesystem::absolute(oOptions.m_oDirectory);
            std::filesystem::create_directories(oRoot);
            if (!std::filesystem::is_directory(oRoot))
            {
                throw Error(ErrorCode::Io, "Export destination is not a directory");
            }
            ExportStaging oStaging(oRoot);
            std::ofstream oText(oStaging.m_oPath / "novel.txt", std::ios::binary);
            std::ofstream oHtml(oStaging.m_oPath / "index.html", std::ios::binary);
            if (!oText || !oHtml)
            {
                throw Error(ErrorCode::Io, "Cannot create exported book files");
            }
            std::map<int, ChapterHeading> vHeadings;
            std::map<std::string, std::string> vImages;
            std::string strTitle;
            DownloadContentVisitor oVisitor;
            oVisitor.m_fnBook = [&](const Novel& oNovel, const std::vector<Volume>& vVolumes,
                                     const std::vector<std::string>& vUrls)
            {
                strTitle = safeTitle(oNovel.m_strTitle);
                oProgress.m_nImagesTotal = oOptions.m_bIncludeImages ? vUrls.size() : 0;
                oText << oNovel.m_strTitle << "\n";
                if (!oNovel.m_strAuthor.empty())
                {
                    oText << "作者：" << oNovel.m_strAuthor << "\n";
                }
                oText << "\n" << oNovel.m_strIntro << "\n\n";
                oHtml << "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
                         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                         "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
                         "img-src 'self'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">"
                         "<title>" << htmlText(oNovel.m_strTitle) << "</title><style>"
                         "body{max-width:44rem;margin:0 auto;padding:1.5rem;font-family:serif;"
                         "line-height:1.8;color:#243047;background:#fcfaf5}h1,h2,h3{line-height:1.4}"
                         "nav{padding:1rem;background:#f0eee8;border-radius:.5rem}nav a{color:#315b46}"
                         ".chapter-text{text-align:justify;overflow-wrap:anywhere}.image-note{color:#697386}"
                         "figure{margin:1.5rem 0;text-align:center}img{max-width:100%;height:auto}"
                         "@media(prefers-color-scheme:dark){body{color:#e7e9ee;background:#20232a}"
                         "nav{background:#2d323c}nav a{color:#b4ddc1}}</style></head><body><header><h1>"
                      << htmlText(oNovel.m_strTitle) << "</h1>";
                if (!oNovel.m_strAuthor.empty())
                {
                    oHtml << "<p>作者：" << htmlText(oNovel.m_strAuthor) << "</p>";
                }
                oHtml << "<p>" << htmlText(oNovel.m_strIntro) << "</p></header><nav aria-label=\"章节目录\">";
                for (const auto& oVolume : vVolumes)
                {
                    oHtml << "<h2>" << htmlText(oVolume.m_strTitle) << "</h2><ol>";
                    bool bFirst = true;
                    for (const auto& oChapter : oVolume.m_vChapters)
                    {
                        vHeadings.emplace(oChapter.m_nId,
                                          ChapterHeading{oVolume.m_strTitle, oChapter.m_strTitle, bFirst});
                        bFirst = false;
                        ++oProgress.m_nChaptersTotal;
                        oHtml << "<li><a href=\"#chapter-" << oChapter.m_nId << "\">"
                              << htmlText(oChapter.m_strTitle) << "</a></li>";
                    }
                    oHtml << "</ol>";
                }
                oHtml << "</nav><main>";
                fnProgress();
            };
            if (oOptions.m_bIncludeImages)
            {
                oVisitor.m_fnImage = [&](const std::string& strUrl, const std::string& strBytes)
                {
                    fnProgress();
                    const std::string strName = "images/image-" + std::to_string(vImages.size() + 1) + imageExtension(strBytes);
                    std::filesystem::create_directories(oStaging.m_oPath / "images");
                    writeBytes(oStaging.m_oPath / strName, strBytes);
                    vImages.emplace(strUrl, strName);
                    ++oProgress.m_nImagesCompleted;
                    fnProgress();
                };
            }
            oVisitor.m_fnChapter = [&](int nChapterId, const std::string& strText)
            {
                fnProgress();
                const auto& oHeading = vHeadings.at(nChapterId);
                if (oHeading.m_bVolumeStart)
                {
                    oText << "\n" << oHeading.m_strVolume << "\n\n";
                    oHtml << "<h2>" << htmlText(oHeading.m_strVolume) << "</h2>";
                }
                oText << oHeading.m_strTitle << "\n\n";
                oHtml << "<section id=\"chapter-" << nChapterId << "\"><h3>"
                      << htmlText(oHeading.m_strTitle) << "</h3><p class=\"chapter-text\">";
                formatText(strText, oOptions.m_bIncludeImages, vImages, oText, oHtml);
                oText << "\n\n";
                oHtml << "</p></section>";
                ++oProgress.m_nChaptersCompleted;
                fnProgress();
            };
            oSource.visitContent(nNovelId, oVisitor);
            oHtml << "</main></body></html>";
            oText.close();
            oHtml.close();
            if (!oText || !oHtml)
            {
                throw Error(ErrorCode::Io, "Cannot finish exported book files");
            }
            fnProgress();
            for (int nAttempt = 0; nAttempt < 32; ++nAttempt)
            {
                // An unpredictable suffix avoids collisions between simultaneous
                // exports and leaves all existing user-named directories untouched.
                const auto oDestination = oRoot / std::filesystem::u8path(
                    strTitle + '-' + std::to_string(nNovelId) + '-' + uniqueToken());
                if (std::filesystem::exists(oDestination))
                {
                    continue;
                }
                std::filesystem::rename(oStaging.m_oPath, oDestination);
                oStaging.m_oPath.clear();
                return {oDestination, oDestination / "novel.txt", oDestination / "index.html",
                        oProgress.m_nChaptersCompleted, oProgress.m_nImagesCompleted};
            }
            throw Error(ErrorCode::Io, "Cannot allocate a unique exported book directory");
        }
        catch (const std::filesystem::filesystem_error&)
        {
            throw Error(ErrorCode::Io, "Cannot access the export directory");
        }
    }
} // namespace Wenku8
