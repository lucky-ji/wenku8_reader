// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QSettings>
#include <QDateTime>
#include <QVector>
#include <optional>
namespace Wenku8::Reader
{
    struct ReadingEntry
    {
        int m_nNovel = 0, m_nChapter = 0, m_nOffset = 0;
        QString m_strTitle, m_strAuthor, m_strChapterTitle, m_strSnippet;
        double m_fProgress = 0;
        QDateTime m_oUpdated;
    };
    struct BookmarkEntry
    {
        QString m_strId, m_strTitle, m_strSnippet;
        int m_nChapter = 0, m_nOffset = 0;
        QDateTime m_oCreated;
    };
    class ReadingState
    {
      public:
        ReadingState(QSettings&, const QString& strOrigin);
        std::optional<ReadingEntry> position(int nNovel) const;
        void save(ReadingEntry, bool bVisited = false);
        QVector<ReadingEntry> history() const;
        QVector<BookmarkEntry> bookmarks(int nNovel);
        QString addBookmark(int nNovel, BookmarkEntry);
        void removeBookmark(int nNovel, const QString& strId);

      private:
        QSettings& m_oSettings;
        QString m_strPrefix;
        QString key(int nNovel) const;
        void migrateBookmark(int nNovel);
    };
} // namespace Wenku8::Reader
