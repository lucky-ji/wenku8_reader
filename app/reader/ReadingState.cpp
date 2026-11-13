// SPDX-License-Identifier: GPL-2.0-only
#include "ReadingState.h"
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace Wenku8::Reader
{
    ReadingState::ReadingState(QSettings& oSettings, const QString& strOrigin)
        : m_oSettings(oSettings), m_strPrefix("reading/" + strOrigin + "/")
    {
    }
    QString ReadingState::key(int nNovel) const
    {
        return m_strPrefix + QString::number(nNovel);
    }
    std::optional<ReadingEntry> ReadingState::position(int nNovel) const
    {
        if (nNovel <= 0)
            return {};
        auto strBase = key(nNovel);
        ReadingEntry oE;
        oE.m_nNovel = nNovel;
        oE.m_nChapter = m_oSettings.value(strBase + "/chapter", 0).toInt();
        if (oE.m_nChapter <= 0)
            return {};
        oE.m_nOffset = std::max(0, m_oSettings.value(strBase + "/offset", 0).toInt());
        oE.m_strTitle = m_oSettings.value(strBase + "/title").toString();
        oE.m_strAuthor = m_oSettings.value(strBase + "/author").toString();
        oE.m_strChapterTitle = m_oSettings.value(strBase + "/chapterTitle").toString();
        oE.m_strSnippet = m_oSettings.value(strBase + "/snippet").toString();
        oE.m_fProgress =
            std::clamp(m_oSettings.value(strBase + "/progress", 0).toDouble(), 0.0, 1.0);
        if (!std::isfinite(oE.m_fProgress))
            oE.m_fProgress = 0;
        oE.m_oUpdated = m_oSettings.value(strBase + "/updated").toDateTime();
        return oE;
    }
    void ReadingState::save(ReadingEntry oE, bool bVisited)
    {
        if (oE.m_nNovel <= 0 || oE.m_nChapter <= 0)
            return;
        auto oPrevious = position(oE.m_nNovel);
        // Idle timers must not reorder history or keep rewriting the same position.
        if (!bVisited && oPrevious && oPrevious->m_nChapter == oE.m_nChapter &&
            oPrevious->m_nOffset == oE.m_nOffset && oPrevious->m_strTitle == oE.m_strTitle &&
            oPrevious->m_strChapterTitle == oE.m_strChapterTitle)
            return;
        if (!std::isfinite(oE.m_fProgress))
            oE.m_fProgress = 0;
        auto strBase = key(oE.m_nNovel);
        m_oSettings.setValue(strBase + "/chapter", oE.m_nChapter);
        m_oSettings.setValue(strBase + "/offset", std::max(0, oE.m_nOffset));
        m_oSettings.setValue(strBase + "/title", oE.m_strTitle);
        m_oSettings.setValue(strBase + "/author", oE.m_strAuthor);
        m_oSettings.setValue(strBase + "/chapterTitle", oE.m_strChapterTitle);
        m_oSettings.setValue(strBase + "/snippet", oE.m_strSnippet.left(140));
        m_oSettings.setValue(strBase + "/progress", std::clamp(oE.m_fProgress, 0.0, 1.0));
        m_oSettings.setValue(strBase + "/updated", oE.m_oUpdated.isValid()
                                                       ? oE.m_oUpdated.toUTC()
                                                       : QDateTime::currentDateTimeUtc());
    }
    QVector<ReadingEntry> ReadingState::history() const
    {
        m_oSettings.beginGroup(m_strPrefix);
        auto vNames = m_oSettings.childGroups();
        m_oSettings.endGroup();
        QVector<ReadingEntry> vResult;
        for (const auto& strName : vNames)
        {
            bool bOk;
            int nId = strName.toInt(&bOk);
            if (!bOk)
                continue;
            auto oE = position(nId);
            if (oE && !oE->m_strTitle.isEmpty() && oE->m_oUpdated.isValid())
                vResult.append(*oE);
        }
        std::sort(vResult.begin(), vResult.end(),
                  [](const ReadingEntry& oA, const ReadingEntry& oB)
                  {
                      return oA.m_oUpdated == oB.m_oUpdated ? oA.m_nNovel < oB.m_nNovel
                                                            : oA.m_oUpdated > oB.m_oUpdated;
                  });
        return vResult;
    }
    void ReadingState::migrateBookmark(int nNovel)
    {
        auto strBase = key(nNovel);
        if (m_oSettings.value(strBase + "/bookmarksMigrated", false).toBool())
            return;
        int nChapter = m_oSettings.value(strBase + "/bookmarkChapter", 0).toInt();
        m_oSettings.setValue(strBase + "/bookmarksMigrated", true);
        if (nChapter > 0)
        {
            BookmarkEntry oE;
            oE.m_nChapter = nChapter;
            oE.m_nOffset = std::max(0, m_oSettings.value(strBase + "/bookmarkOffset", 0).toInt());
            oE.m_strTitle = "原有书签";
            addBookmark(nNovel, oE);
        }
    }
    QVector<BookmarkEntry> ReadingState::bookmarks(int nNovel)
    {
        if (nNovel <= 0)
            return {};
        migrateBookmark(nNovel);
        auto strBase = key(nNovel) + "/bookmarks/";
        m_oSettings.beginGroup(strBase);
        auto vIds = m_oSettings.childGroups();
        m_oSettings.endGroup();
        QVector<BookmarkEntry> vResult;
        for (const auto& strId : vIds)
        {
            if (QUuid(strId).isNull())
                continue;
            auto strItem = strBase + strId;
            BookmarkEntry oE;
            oE.m_strId = strId;
            oE.m_nChapter = m_oSettings.value(strItem + "/chapter", 0).toInt();
            if (oE.m_nChapter <= 0)
                continue;
            oE.m_nOffset = std::max(0, m_oSettings.value(strItem + "/offset", 0).toInt());
            oE.m_strTitle = m_oSettings.value(strItem + "/title").toString();
            oE.m_strSnippet = m_oSettings.value(strItem + "/snippet").toString();
            oE.m_oCreated = m_oSettings.value(strItem + "/created").toDateTime();
            vResult.append(oE);
        }
        std::sort(vResult.begin(), vResult.end(),
                  [](const BookmarkEntry& oA, const BookmarkEntry& oB)
                  {
                      return oA.m_oCreated > oB.m_oCreated;
                  });
        return vResult;
    }
    QString ReadingState::addBookmark(int nNovel, BookmarkEntry oE)
    {
        if (nNovel <= 0 || oE.m_nChapter <= 0)
            return {};
        oE.m_nOffset = std::max(0, oE.m_nOffset);
        for (const auto& oOld : bookmarks(nNovel))
            if (oOld.m_nChapter == oE.m_nChapter && oOld.m_nOffset == oE.m_nOffset)
                return oOld.m_strId;
        oE.m_strId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto strBase = key(nNovel) + "/bookmarks/" + oE.m_strId;
        m_oSettings.setValue(strBase + "/chapter", oE.m_nChapter);
        m_oSettings.setValue(strBase + "/offset", oE.m_nOffset);
        m_oSettings.setValue(strBase + "/title", oE.m_strTitle.left(200));
        m_oSettings.setValue(strBase + "/snippet", oE.m_strSnippet.left(140));
        m_oSettings.setValue(strBase + "/created", oE.m_oCreated.isValid()
                                                        ? oE.m_oCreated : QDateTime::currentDateTimeUtc());
        m_oSettings.sync();
        return oE.m_strId;
    }
    void ReadingState::removeBookmark(int nNovel, const QString& strId)
    {
        if (nNovel > 0 && !QUuid(strId).isNull())
        {
            m_oSettings.remove(key(nNovel) + "/bookmarks/" + strId);
            m_oSettings.sync();
        }
    }
} // namespace Wenku8::Reader
