// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8::Detail
{
    std::string transcode(const std::string&, const std::string& strFrom, const std::string& strTo);
    class WebBackend
    {
      public:
        WebBackend(ClientOptions, std::shared_ptr<HttpTransport>);
        ~WebBackend();
        NovelPage list(Sort, int, CachePolicy);
        NovelPage search(const std::string&, SearchType, CachePolicy);
        Novel novel(int, CachePolicy);
        std::string introduction(int, CachePolicy);
        std::vector<Volume> index(int, CachePolicy);
        std::string chapter(int, int, CachePolicy);
        std::string cover(int, CachePolicy);
        std::vector<Library> libraries(CachePolicy);
        NovelPage library(int, int, CachePolicy);
        std::string captcha();
        LoginRequirements prepareLogin();
        UserProfile restoreSession(const std::string&);
        User login(const std::string&, const std::string&, const std::string&);
        void logout();
        std::optional<User> user() const;
        UserProfile userInfo();
        std::vector<Novel> bookshelf();
        void addToBookshelf(int);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
} // namespace Wenku8::Detail
