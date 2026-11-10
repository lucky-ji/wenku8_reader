// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#ifndef Q_MOC_RUN
#include "Wenku8Sdk.h"
#endif
#include <QObject>
#include <QList>
#include <QString>
#include <memory>
namespace Wenku8::Reader
{
    enum class DownloadTaskState
    {
        Queued,
        Running,
        Paused,
        Failed,
        Complete
    };
    struct DownloadTask
    {
        QString m_strId, m_strTitle, m_strAccount, m_strDestination, m_strLocation, m_strMessage;
        int m_nNovelId = 0;
        bool m_bLocal = false, m_bImages = true;
        DownloadTaskState m_eState = DownloadTaskState::Queued;
        qint64 m_nChaptersCompleted = 0, m_nChaptersTotal = 0,
               m_nImagesCompleted = 0, m_nImagesTotal = 0;
    };
    // Public operations belong to the owning Qt thread. Downloads use a single
    // independent worker; no password, cookies or session headers are persisted.
    class DownloadManager final : public QObject
    {
        Q_OBJECT
      public:
        DownloadManager(const QString& strCacheDirectory, const QString& strOrigin,
                        QObject* pParent = nullptr);
        ~DownloadManager() override;
        QList<DownloadTask> tasks() const;
        void setSession(const ClientOptions&, std::shared_ptr<HttpTransport>, const QString& strAccount);
        void suspend();
        void shutdown();
        QString enqueue(int nId, const QString& strTitle, bool bImages,
                        const QString& strDestination = {});
        void pause(const QString& strId);
        void resume(const QString& strId);
        void remove(const QString& strId);
      signals:
        void tasksChanged();
        void completed(int nNovelId, bool bLocal, const QString& strLocation);
        void notice(const QString& strMessage);
      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
}
