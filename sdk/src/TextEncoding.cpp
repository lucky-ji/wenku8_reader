// SPDX-License-Identifier: GPL-2.0-only
#include "WebBackend.h"
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <iconv.h>
#include <cerrno>
#endif
namespace Wenku8::Detail
{
    std::string transcode(const std::string& strBytes, const std::string& strFrom,
                          const std::string& strTo)
    {
        if (strBytes.empty())
            return {};
        if (strFrom == strTo)
            return strBytes;
#ifdef _WIN32
        auto fnPage = [](const std::string& strName) -> unsigned
        {
            if (strName == "UTF-8")
                return CP_UTF8;
            if (strName == "GBK")
                return 936;
            if (strName == "BIG5")
                return 950;
            throw Error(ErrorCode::InvalidArgument, "Unsupported text encoding");
        };
        if (strBytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            throw Error(ErrorCode::ResponseTooLarge, "Text too large");
        auto nSrc = fnPage(strFrom), nDst = fnPage(strTo);
        int nSize = MultiByteToWideChar(nSrc, MB_ERR_INVALID_CHARS, strBytes.data(),
                                        static_cast<int>(strBytes.size()), nullptr, 0);
        if (!nSize)
            throw Error(ErrorCode::Parse, "Invalid encoded text");
        std::wstring strWide(static_cast<std::size_t>(nSize), L'\0');
        if (!MultiByteToWideChar(nSrc, MB_ERR_INVALID_CHARS, strBytes.data(),
                                 static_cast<int>(strBytes.size()), &strWide[0], nSize))
            throw Error(ErrorCode::Parse, "Cannot decode text");
        BOOL nReplaced = FALSE;
        auto nFlags = nDst == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        nSize = WideCharToMultiByte(nDst, nFlags, strWide.data(), static_cast<int>(strWide.size()),
                                    nullptr, 0, nullptr, nDst == CP_UTF8 ? nullptr : &nReplaced);
        if (!nSize || nReplaced)
            throw Error(ErrorCode::InvalidArgument,
                        "Text cannot be represented in destination encoding");
        std::string strResult(static_cast<std::size_t>(nSize), '\0');
        if (!WideCharToMultiByte(nDst, nFlags, strWide.data(), static_cast<int>(strWide.size()),
                                 &strResult[0], nSize, nullptr,
                                 nDst == CP_UTF8 ? nullptr : &nReplaced) ||
            nReplaced)
            throw Error(ErrorCode::Parse, "Cannot encode text");
        return strResult;
#else
        auto pConverter = iconv_open(strTo.c_str(), strFrom.c_str());
        if (pConverter == reinterpret_cast<iconv_t>(-1))
            throw Error(ErrorCode::Unsupported, "Encoding conversion unavailable");
        struct Close
        {
            iconv_t m_pHandle;
            ~Close()
            {
                iconv_close(m_pHandle);
            }
        } oClose{pConverter};
        std::string strOutput(strBytes.size() * 4 + 32, '\0');
        char* pInput = const_cast<char*>(strBytes.data());
        std::size_t nLeft = strBytes.size();
        char* pOut = &strOutput[0];
        std::size_t nCapacity = strOutput.size();
        if (iconv(pConverter, &pInput, &nLeft, &pOut, &nCapacity) == static_cast<std::size_t>(-1) ||
            nLeft)
            throw Error(ErrorCode::Parse, "Invalid or unrepresentable encoded text");
        strOutput.resize(strOutput.size() - nCapacity);
        return strOutput;
#endif
    }
} // namespace Wenku8::Detail
