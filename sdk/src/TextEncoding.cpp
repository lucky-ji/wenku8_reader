// SPDX-License-Identifier: GPL-2.0-only
#include "WebBackend.h"
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(WENKU8_HARMONY_ICU)
#include <unicode/ucnv.h>
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
#elif defined(WENKU8_HARMONY_ICU)
        // The base musl iconv implementation can decode GBK and Big5, but
        // cannot reliably encode search keywords into these charsets.
        auto fnOpen = [](const std::string& strEncoding)
        {
            UErrorCode eError = U_ZERO_ERROR;
            std::unique_ptr<UConverter, decltype(&ucnv_close)> pConverter(
                ucnv_open(strEncoding.c_str(), &eError), ucnv_close);
            if (U_FAILURE(eError) || !pConverter)
            {
                throw Error(ErrorCode::Unsupported, "Encoding conversion unavailable");
            }
            return pConverter;
        };
        auto pFrom = fnOpen(strFrom);
        auto pTo = fnOpen(strTo);
        UErrorCode eError = U_ZERO_ERROR;
        // Leave ICU's incoming conversion error intact instead of substituting text.
        ucnv_setToUCallBack(
            pFrom.get(), [](const void*, UConverterToUnicodeArgs*, const char*, int32_t,
                            UConverterCallbackReason, UErrorCode*)
            {
            },
            nullptr, nullptr, nullptr, &eError);
        ucnv_setFromUCallBack(
            pTo.get(), [](const void*, UConverterFromUnicodeArgs*, const UChar*, int32_t,
                          UChar32, UConverterCallbackReason, UErrorCode*)
            {
            },
            nullptr, nullptr, nullptr, &eError);
        if (strBytes.size() > (std::numeric_limits<std::size_t>::max() - 32) / 4)
        {
            throw Error(ErrorCode::ResponseTooLarge, "Text too large");
        }
        std::string strOutput(strBytes.size() * 4 + 32, '\0');
        const char* pInput = strBytes.data();
        char* pOutput = strOutput.data();
        ucnv_convertEx(pTo.get(), pFrom.get(), &pOutput, strOutput.data() + strOutput.size(), &pInput,
                       strBytes.data() + strBytes.size(), nullptr, nullptr, nullptr, nullptr,
                       true, true, &eError);
        if (U_FAILURE(eError) || pInput != strBytes.data() + strBytes.size())
        {
            throw Error(ErrorCode::Parse, "Invalid or unrepresentable encoded text");
        }
        strOutput.resize(static_cast<std::size_t>(pOutput - strOutput.data()));
        return strOutput;
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
