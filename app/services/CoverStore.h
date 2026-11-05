// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
#include <QObject>
#include <QImage>
#include <QPixmap>
#include <QCache>
#include <QSet>
#include <QHash>
#include <QThreadPool>
#include <atomic>
namespace Wenku8::Reader
{
    QImage decodeCover(const QByteArray&);
    QString coverUrl(int nNovel);
    QPixmap coverPixmap(const QImage&, QSize oLogicalSize, qreal fDevicePixelRatio);
    class CoverStore final : public QObject
    {
        Q_OBJECT
      public:
        CoverStore(QString strOrigin, QString strCacheDirectory, QObject* pParent = nullptr);
        ~CoverStore() override;
        QImage image(int nNovel) const;
        bool unavailable(int nNovel) const;
        QString placeholder(int nNovel) const;
        void request(int nNovel, const QByteArray& baDownloadedCover = {});
        void cancelPending();
        void retryFailures();
      signals:
        void ready(int nNovel, const QImage& oImage);
        void statusChanged(int nNovel);

      private:
        QString m_strOrigin, m_strDirectory;
        QCache<int, QImage> m_vImages{32 * 1024}; // KiB, bounded thumbnails.
        QSet<int> m_vPending;
        QHash<int, qint64> m_vFailed;
        QSet<int> m_vMissing;
        QHash<int, unsigned> m_vRounds;
        QThreadPool m_oPool;
        std::shared_ptr<std::atomic_bool> m_pCancelled = std::make_shared<std::atomic_bool>(false);
        quint64 m_nGeneration = 0;
    };
} // namespace Wenku8::Reader
