// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8
{
    // Historical protocol documented by eclipse-android-old/LightNovelLibrary.
    class WENKU8_API LegacyApi
    {
      public:
        static HttpRequest request(const std::string& strEndpoint, const Fields& vParameters,
                                   const std::string& strAppVersion = {});
        static Fields book(const std::string& strOperation, int nId, Language);
        static Fields list(Sort, int nPage, Language);
        static Fields search(const std::string&, SearchType, Language);
        static Fields library(int nId, int nPage, Language);
    };
    // Explicit opt-in to the public historical v1.9 login/logout convention.
    WENKU8_API std::shared_ptr<AuthenticationProtocol> makeLegacyAuthentication(
        const std::string& strEndpoint, const std::string& strAppVersion = {});
    namespace Parser
    {
        WENKU8_API NovelPage novelPage(const std::string&);
        WENKU8_API Novel novel(const std::string&);
        WENKU8_API std::vector<Volume> index(const std::string&);
        WENKU8_API std::vector<Library> libraries(const std::string&);
        WENKU8_API UserProfile userInfo(const std::string&);
        WENKU8_API std::vector<Novel> bookshelf(const std::string&);
        WENKU8_API void operationSucceeded(const std::string&);
    } // namespace Parser
} // namespace Wenku8
