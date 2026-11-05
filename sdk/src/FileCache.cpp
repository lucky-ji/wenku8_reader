// SPDX-License-Identifier: GPL-2.0-only
#include "Storage.h"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace Wenku8
{
    namespace Detail
    {
        void atomicWrite(const std::filesystem::path& oTarget, const std::string& strBytes)
        {
            if (oTarget.empty() || oTarget.filename().empty())
                throw Error(ErrorCode::InvalidArgument, "Empty output filename");
            std::error_code oEc;
            if (!oTarget.parent_path().empty())
            {
                std::filesystem::create_directories(oTarget.parent_path(), oEc);
                if (oEc)
                    throw Error(ErrorCode::Io, "Cannot create output directory");
            }
            static std::atomic<std::uint64_t> oSerial{0};
            std::random_device oRandom;
            auto oTemp = oTarget;
            oTemp += ".tmp-" + std::to_string(oRandom()) + "-" + std::to_string(++oSerial);
            struct Cleanup
            {
                std::filesystem::path m_oPath;
                ~Cleanup()
                {
                    std::error_code oIgnored;
                    std::filesystem::remove(m_oPath, oIgnored);
                }
            } oCleanup{oTemp};
            {
                std::ofstream oOut(oTemp, std::ios::binary | std::ios::trunc);
                if (!oOut)
                    throw Error(ErrorCode::Io, "Cannot open temporary output");
                oOut.write(strBytes.data(), static_cast<std::streamsize>(strBytes.size()));
                oOut.flush();
                if (!oOut)
                    throw Error(ErrorCode::Io, "Cannot write output");
                oOut.close();
                if (!oOut)
                    throw Error(ErrorCode::Io, "Cannot close output");
            }
#ifdef _WIN32
            if (!MoveFileExW(oTemp.c_str(), oTarget.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw Error(ErrorCode::Io, "Cannot replace output file");
#else
            std::filesystem::rename(oTemp, oTarget, oEc);
            if (oEc)
                throw Error(ErrorCode::Io, "Cannot replace output file");
#endif
        }
    } // namespace Detail
    FileCache::FileCache(std::filesystem::path oRoot) : m_oRoot(std::move(oRoot))
    {
        if (m_oRoot.empty())
            throw Error(ErrorCode::InvalidArgument, "Empty cache directory");
    }
    std::filesystem::path FileCache::path(const std::string& strKey) const
    {
        // Hash is a filename only, not an identity/security check: the full key is verified on
        // read.
        std::uint64_t nHash = 14695981039346656037ULL;
        for (unsigned char nC : strKey)
        {
            nHash ^= nC;
            nHash *= 1099511628211ULL;
        }
        std::ostringstream oName;
        oName << std::hex << std::setw(16) << std::setfill('0') << nHash << ".w8cache";
        return m_oRoot / oName.str();
    }
    std::optional<std::string> FileCache::read(const std::string& strKey,
                                               std::chrono::seconds oAge) const
    {
        if (oAge.count() < 0)
            throw Error(ErrorCode::InvalidArgument, "Negative cache lifetime");
        auto oFile = path(strKey);
        std::error_code oEc;
        auto oModified = std::filesystem::last_write_time(oFile, oEc);
        if (oEc)
            return std::nullopt;
        if (std::filesystem::file_time_type::clock::now() - oModified > oAge)
            return std::nullopt;
        auto nSize = std::filesystem::file_size(oFile, oEc);
        if (oEc || nSize > 128ULL * 1024 * 1024)
            return std::nullopt;
        std::ifstream oIn(oFile, std::ios::binary);
        if (!oIn)
            return std::nullopt;
        const auto strPrefix =
            std::string("W8CACHE2\n") + std::to_string(strKey.size()) + "\n" + strKey + "\n";
        if (nSize < strPrefix.size() + 8)
            return std::nullopt;
        std::string strStored(strPrefix.size(), '\0');
        if (!oIn.read(&strStored[0], static_cast<std::streamsize>(strStored.size())) ||
            strStored != strPrefix)
            return std::nullopt;
        unsigned char vLength[8]{};
        if (!oIn.read(reinterpret_cast<char*>(vLength), 8))
            return std::nullopt;
        std::uint64_t nCount = 0;
        for (unsigned nI = 0; nI < 8; ++nI)
            nCount |= static_cast<std::uint64_t>(vLength[nI]) << (nI * 8);
        if (nCount != nSize - strPrefix.size() - 8)
            return std::nullopt;
        std::string strBytes(static_cast<std::size_t>(nCount), '\0');
        if (!strBytes.empty() &&
            !oIn.read(&strBytes[0], static_cast<std::streamsize>(strBytes.size())))
            return std::nullopt;
        return strBytes;
    }
    void FileCache::write(const std::string& strKey, const std::string& strBytes)
    {
        if (strKey.size() > 1024 * 1024 || strBytes.size() > 120ULL * 1024 * 1024)
            throw Error(ErrorCode::InvalidArgument, "Cache entry too large");
        std::string strLength(8, '\0');
        for (unsigned nI = 0; nI < 8; ++nI)
            strLength[nI] =
                static_cast<char>((static_cast<std::uint64_t>(strBytes.size()) >> (nI * 8)) & 255);
        Detail::atomicWrite(path(strKey), "W8CACHE2\n" + std::to_string(strKey.size()) + "\n" +
                                              strKey + "\n" + strLength + strBytes);
    }
    void FileCache::remove(const std::string& strKey)
    {
        std::error_code oEc;
        std::filesystem::remove(path(strKey), oEc);
        if (oEc)
            throw Error(ErrorCode::Io, "Cannot remove cache entry");
    }
} // namespace Wenku8
