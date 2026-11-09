// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#ifndef Q_MOC_RUN
#include "Wenku8Sdk.h"
#endif
#include "Theme.h"
#include <QObject>
#include <memory>
class QWidget;
namespace Wenku8::Reader
{
    class DownloadManager;
    class OnlineSession final : public QObject
    {
        Q_OBJECT
      public:
        OnlineSession(QWidget* pOwner, const QString& strOrigin, const QString& strDirectory,
                      const QString& strSessionDirectory = {});
        ~OnlineSession() override;
        bool authenticated() const;
        bool busy() const;
        bool loginVisible() const;
        QString username() const;
        const std::vector<Novel>& novels() const;
        const Novel& novel() const;
        const std::vector<Volume>& volumes() const;
        QString chapterText() const;
        QByteArray imageBytes(const QString&) const;
        void signIn(bool bReset = false);
        void restoreSession();
        bool persistent() const;
        void setTheme(Theme);
        void shelf(bool bForce = false);
        void latest(int nPage = 1, bool bForce = false);
        void search(const QString&, bool bForce = false);
        void openBook(int nId, bool bForce = false);
        void openChapter(int nNovel, int nChapter, bool bForce = false);
        DownloadManager* downloads() const;
        void addToShelf(int nId);
        void cancel();
        bool hasRetryableRequest() const;
        void retryLastRequest();
      signals:
        void authenticationChanged(bool);
        void restorationChanged(bool);
        void busyChanged(bool);
        void statusChanged(const QString&);
        void progressChanged(qint64 nCurrent, qint64 nTotal);
        void novelsReady();
        void bookshelfReady();
        void bookshelfAdded(int);
        void bookReady();
        void chapterReady();
        void chapterImagesReady(int nNovel, int nChapter);
        void retryAvailableChanged(bool);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };
} // namespace Wenku8::Reader
