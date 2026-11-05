// SPDX-License-Identifier: GPL-2.0-only
#include "TextPackage.h"
#include "Storage.h"
#include <libxml/xmlstring.h>
#include <algorithm>
#include <cctype>
#include <regex>
namespace Wenku8::Detail
{
    namespace
    {
        void validate(const std::string& strBody)
        {
            auto strPrefix = strBody.substr(0, 4096);
            auto strLowered = strPrefix;
            std::transform(strLowered.begin(), strLowered.end(), strLowered.begin(),
                           [](unsigned char nC)
                           {
                               return nC < 128 ? static_cast<char>(std::tolower(nC))
                                               : static_cast<char>(nC);
                           });
            if (strBody.size() < 64 || strBody.find('\0') != std::string::npos ||
                !xmlCheckUTF8(BAD_CAST strBody.c_str()) ||
                strPrefix.find(u8"轻小说文库") == std::string::npos ||
                !std::regex_search(strPrefix,
                                   std::regex("(?:^|[\\r\\n])<[^<>\\r\\n]+>(?:[\\r\\n]|$)")) ||
                strLowered.find("<html") != std::string::npos ||
                strLowered.find("<!doctype") != std::string::npos ||
                strLowered.find("challenge-platform") != std::string::npos)
                throw Error(ErrorCode::Parse,
                            "Response is not a recognized UTF-8 Wenku8 text package");
        }
    } // namespace
    void downloadTextPackage(int nId, const std::filesystem::path& oDestination,
                             CachePolicy ePolicy, HttpTransport& oTransport,
                             const TransferOptions& oTransfer, FileCache* pCache,
                             std::chrono::seconds oAge)
    {
        if (nId <= 0 || oDestination.empty() || oDestination.filename().empty())
            throw Error(ErrorCode::InvalidArgument, "Invalid package ID or destination");
        if (ePolicy != CachePolicy::NetworkOnly && ePolicy != CachePolicy::PreferCache &&
            ePolicy != CachePolicy::CacheOnly && ePolicy != CachePolicy::Refresh)
            throw Error(ErrorCode::InvalidArgument, "Invalid package cache policy");
        const auto strUrl =
            "https://dl.wenku8.com/down.php?type=utf8&node=1&id=" + std::to_string(nId);
        const auto strKey = "official-txt-utf8-v1\n" + strUrl;
        if (pCache && (ePolicy == CachePolicy::PreferCache || ePolicy == CachePolicy::CacheOnly))
        {
            if (auto oBytes = pCache->read(strKey, oAge))
            {
                bool bValid = true;
                try
                {
                    validate(*oBytes);
                }
                catch (const Error&)
                {
                    bValid = false;
                }
                if (bValid)
                {
                    atomicWrite(oDestination, *oBytes);
                    return;
                }
            }
        }
        if (ePolicy == CachePolicy::CacheOnly)
            throw Error(ErrorCode::CacheMiss, "No valid UTF-8 text package in cache");
        auto oResponse = oTransport.perform({strUrl, "GET", {}, {}}, oTransfer);
        if (oResponse.m_nStatus != 200)
            throw Error(ErrorCode::Http, "Text package request failed", oResponse.m_nStatus);
        validate(oResponse.m_strBody);
        if (pCache && ePolicy != CachePolicy::NetworkOnly)
            pCache->write(strKey, oResponse.m_strBody);
        atomicWrite(oDestination, oResponse.m_strBody);
    }
} // namespace Wenku8::Detail
