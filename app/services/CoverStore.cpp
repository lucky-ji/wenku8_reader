// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
#include "CoverStore.h"
#include "NetworkRetry.h"
#include "RasterImage.h"
#include <QBuffer>
#include <QImageReader>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QDateTime>
#include <QRunnable>
#include <QTimer>
namespace Wenku8::Reader
{
    QString coverUrl(int nNovel)
    {
        return QString("https://img.wenku8.com/image/%1/%2/%2.jpg").arg(nNovel / 1000).arg(nNovel);
    }
    QPixmap coverPixmap(const QImage& oImage, QSize oLogicalSize, qreal fDpr)
    {
        if (oImage.isNull() || oLogicalSize.isEmpty())
            return {};
        fDpr = qMax(qreal(1), fDpr);
        auto oPixmap = QPixmap::fromImage(
            oImage.scaled(oLogicalSize * fDpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        oPixmap.setDevicePixelRatio(fDpr);
        return oPixmap;
    }
    QImage decodeCover(const QByteArray& baBytes)
    {
        if (baBytes.isEmpty() || baBytes.size() > 4 * 1024 * 1024)
            return {};
        const auto baFormat = rasterFormat(baBytes);
        if (baFormat.isEmpty())
            return {};
        QBuffer oBuffer;
        oBuffer.setData(baBytes);
        oBuffer.open(QIODevice::ReadOnly);
        QImageReader oReader(&oBuffer, baFormat);
        oReader.setDecideFormatFromContent(false);
        oReader.setAutoTransform(true);
        QSize oSize = oReader.size();
        if (!oSize.isValid() || qint64(oSize.width()) * oSize.height() > 20000000)
            return {};
        if (oSize.width() > 1080 || oSize.height() > 1620)
            oReader.setScaledSize(oSize.scaled(1080, 1620, Qt::KeepAspectRatio));
        return oReader.read();
    }
    CoverStore::CoverStore(QString strOrigin, QString strCache, QObject* pParent)
        : QObject(pParent), m_strOrigin(std::move(strOrigin))
    {
        auto strSpace = QString::fromLatin1(
            QCryptographicHash::hash(m_strOrigin.toUtf8(), QCryptographicHash::Sha256).toHex());
        m_strDirectory = QDir(strCache).filePath("covers-original-v2/" + strSpace);
        m_oPool.setMaxThreadCount(3);
    }
    CoverStore::~CoverStore()
    {
        m_pCancelled->store(true);
        m_oPool.waitForDone();
    }
    QImage CoverStore::image(int nId) const
    {
        auto pImage = m_vImages.object(nId);
        return pImage ? *pImage : QImage();
    }
    bool CoverStore::unavailable(int nId) const
    {
        return m_vFailed.contains(nId) && !m_vImages.contains(nId);
    }
    QString CoverStore::placeholder(int nId) const
    {
        if (m_vPending.contains(nId))
            return "加载封面…";
        if (m_vMissing.contains(nId))
            return "暂无封面";
        if (m_vFailed.contains(nId))
            return m_vRounds.value(nId) < 2 ? "加载失败，稍后重试" : "加载失败，可刷新重试";
        return "加载封面…";
    }
    void CoverStore::cancelPending()
    {
        m_pCancelled->store(true);
        m_pCancelled = std::make_shared<std::atomic_bool>(false);
        ++m_nGeneration;
        m_vPending.clear();
        m_vRounds.clear();
        m_vFailed.clear();
        m_vMissing.clear();
    }
    void CoverStore::retryFailures()
    {
        m_vFailed.clear();
        m_vMissing.clear();
        m_vRounds.clear();
    }
    void CoverStore::request(int nId, const QByteArray& baDownloaded)
    {
        if (nId <= 0 || m_vImages.contains(nId) || m_vPending.contains(nId))
            return;
        if (baDownloaded.isEmpty() &&
            m_vFailed.value(nId, 0) > QDateTime::currentMSecsSinceEpoch() - 60000)
            return;
        if (m_vFailed.contains(nId))
        {
            m_vFailed.remove(nId);
            m_vRounds.remove(nId);
        }
        m_vMissing.remove(nId);
        m_vPending.insert(nId);
        ++m_vRounds[nId];
        emit statusChanged(nId);
        auto pFlag = m_pCancelled;
        auto nGeneration = m_nGeneration;
        auto strDirectory = m_strDirectory;
        auto strOrigin = m_strOrigin;
        m_oPool.start(QRunnable::create(
            [this, nId, baDownloaded, pFlag, nGeneration, strDirectory, strOrigin]
            {
                QImage oImage;
                bool bMissing = false;
                const auto strPath = QDir(strDirectory).filePath(QString::number(nId) + ".png");
                auto fnRead = [&](const QString& strFile)
                {
                    QFile oInput(strFile);
                    if (oInput.open(QIODevice::ReadOnly) && oInput.size() <= 4 * 1024 * 1024)
                        return decodeCover(oInput.readAll());
                    return QImage();
                };
                if (!pFlag->load())
                    oImage = fnRead(strPath);
                if (oImage.isNull() && !pFlag->load())
                {
                    auto oLocal = decodeCover(baDownloaded);
                    if (oLocal.width() >= 360)
                        oImage = oLocal;
                }
                if (oImage.isNull() && !pFlag->load())
                {
                    try
                    {
                        ClientOptions oOptions;
                        oOptions.m_strEndpoint = strOrigin.toStdString();
                        oOptions.m_eProtocol = Protocol::Website;
                        oOptions.m_oTransfer.m_bFollowRedirects = false;
                        oOptions.m_oTransfer.m_oConnectTimeout = std::chrono::seconds(5);
                        oOptions.m_oTransfer.m_oTimeout = std::chrono::seconds(30);
                        oOptions.m_oTransfer.m_nMaxResponseBytes = 4 * 1024 * 1024;
                        oOptions.m_oTransfer.m_fnProgress = [pFlag](auto, auto)
                        {
                            return !pFlag->load();
                        };
                        // The canonical cover is an independent public image node. Never use the
                        // browser transport.
                        auto oRetry = clientAssetRetryOptions();
                        oRetry.m_oAttemptTimeout = std::chrono::seconds(8);
                        oRetry.m_fnAcceptResponse = [](const HttpResponse& oResponse)
                        {
                            return !decodeCover(QByteArray(oResponse.m_strBody.data(),
                                                           int(oResponse.m_strBody.size())))
                                        .isNull();
                        };
                        auto pAssets = makeRetryingTransport(makeClientAssetTransport(), oRetry);
                        HttpRequest oRequest;
                        oRequest.m_strUrl = coverUrl(nId).toStdString();
                        auto oResponse = pAssets->perform(oRequest, oOptions.m_oTransfer);
                        // Only a confirmed absent original permits the legacy thumbnail fallback.
                        if ((oResponse.m_nStatus == 404 || oResponse.m_nStatus == 410) &&
                            !pFlag->load())
                        {
                            oRequest.m_strUrl =
                                QString("https://img.wenku8.com/image/%1/%2/%2s.jpg")
                                    .arg(nId / 1000)
                                    .arg(nId)
                                    .toStdString();
                            oResponse = pAssets->perform(oRequest, oOptions.m_oTransfer);
                        }
                        bMissing = oResponse.m_nStatus == 404 || oResponse.m_nStatus == 410;
                        if (oResponse.m_nStatus == 200)
                            oImage = decodeCover(QByteArray(oResponse.m_strBody.data(),
                                                            int(oResponse.m_strBody.size())));
                    }
                    catch (const Error& oError)
                    {
                        bMissing = oError.httpStatus() == 404 || oError.httpStatus() == 410;
                    }
                    catch (const std::exception&)
                    {
                    }
                }
                // A previously downloaded thumbnail remains usable without a network connection.
                const bool bDownloadedFallback = oImage.isNull() && !baDownloaded.isEmpty();
                if (bDownloadedFallback && !pFlag->load())
                    oImage = decodeCover(baDownloaded);
                if (!oImage.isNull() && !bDownloadedFallback && !pFlag->load())
                {
                    QDir().mkpath(strDirectory);
                    QSaveFile oOutput(strPath);
                    if (oOutput.open(QIODevice::WriteOnly))
                    {
                        if (oImage.save(&oOutput, "PNG"))
                            oOutput.commit();
                        else
                            oOutput.cancelWriting();
                    }
                }
                if (pFlag->load())
                    return;
                QMetaObject::invokeMethod(
                    this,
                    [this, nId, oImage, bMissing, nGeneration]
                    {
                        if (nGeneration != m_nGeneration)
                            return;
                        m_vPending.remove(nId);
                        if (!oImage.isNull())
                        {
                            m_vImages.insert(nId, new QImage(oImage),
                                             int(qMax<qsizetype>(1, oImage.sizeInBytes() / 1024)));
                            m_vFailed.remove(nId);
                            m_vMissing.remove(nId);
                            m_vRounds.remove(nId);
                        }
                        else
                        {
                            m_vFailed.insert(nId, QDateTime::currentMSecsSinceEpoch());
                            if (bMissing)
                                m_vMissing.insert(nId);
                            if (!bMissing && m_vRounds.value(nId) < 2)
                                QTimer::singleShot(
                                    10000, this,
                                    [this, nId, nGeneration, nFailedAt = m_vFailed.value(nId)]
                                    {
                                        if (nGeneration != m_nGeneration ||
                                            m_vFailed.value(nId) != nFailedAt ||
                                            !m_vFailed.contains(nId) || m_vPending.contains(nId))
                                            return;
                                        m_vFailed.remove(nId);
                                        request(nId);
                                    });
                        }
                        emit statusChanged(nId);
                        emit ready(nId, oImage);
                    },
                    Qt::QueuedConnection);
            }));
    }
} // namespace Wenku8::Reader
