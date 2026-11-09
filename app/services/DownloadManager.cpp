// SPDX-License-Identifier: GPL-2.0-only
#include "DownloadManager.h"
#include "Wenku8Download.h"
#include "ClientNetwork.h"
#include "NetworkRetry.h"
#include "ExportDestination.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QUuid>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace Wenku8::Reader
{
    namespace
    {
        std::filesystem::path nativePath(const QString& strPath)
        {
#ifdef Q_OS_WIN
            return std::filesystem::path(strPath.toStdWString());
#else
            return std::filesystem::u8path(strPath.toUtf8().constData());
#endif
        }
        bool validId(const QString& strId)
        {
            const QUuid oId(strId);
            return !oId.isNull() && oId.toString(QUuid::WithoutBraces) == strId;
        }
    }
    struct DownloadManager::Impl
    {
        DownloadManager* m_pQ;
        QString m_strCache, m_strRoot, m_strOrigin, m_strAccount, m_strActive;
        mutable std::mutex m_oMutex, m_oSnapshotMutex;
        QList<DownloadTask> m_vSnapshot;
        std::condition_variable m_oWake;
        QList<DownloadTask> m_vTasks;
        ClientOptions m_oOptions;
        std::shared_ptr<HttpTransport> m_pTransport;
        std::shared_ptr<std::atomic_bool> m_pCancel;
        std::thread m_oWorker;
        std::atomic_bool m_bStopped{false}, m_bFinished{false};
        bool m_bSession = false;
        std::chrono::steady_clock::time_point m_oLastNotify{};
        Impl(DownloadManager* pQ, QString strCache, QString strOrigin)
            : m_pQ(pQ), m_strCache(QDir(strCache).absolutePath()),
              m_strRoot(QDir(m_strCache).filePath(".downloads")), m_strOrigin(std::move(strOrigin))
        {
            load();
            m_vSnapshot = m_vTasks;
            m_oWorker = std::thread([this]
            {
                work();
                m_bFinished.store(true);
            });
        }
        DownloadTask* find(const QString& strId)
        {
            for (auto& oTask : m_vTasks)
            {
                if (oTask.m_strId == strId)
                {
                    return &oTask;
                }
            }
            return nullptr;
        }
        void post(std::function<void()> fnEvent)
        {
            if (m_bStopped.load())
            {
                return;
            }
            QMetaObject::invokeMethod(m_pQ, [this, fnEvent]
            {
                if (!m_bStopped.load())
                {
                    fnEvent();
                }
            }, Qt::QueuedConnection);
        }
        void changed()
        {
            // Publish a short-lived in-memory snapshot. Painting/navigating the
            // task page must never wait for the checkpoint's filesystem commit.
            {
                std::lock_guard<std::mutex> oSnapshotLock(m_oSnapshotMutex);
                m_vSnapshot = m_vTasks;
            }
            post([this]
            {
                emit m_pQ->tasksChanged();
            });
        }
        void warn(const QString& strMessage)
        {
            post([this, strMessage]
            {
                emit m_pQ->notice(strMessage);
            });
        }
        // Called with the task mutex held; writes are serialized across UI and worker.
        bool save()
        {
            QJsonArray vTasks;
            for (const auto& oTask : m_vTasks)
            {
                QJsonObject oJson;
                oJson["id"] = oTask.m_strId;
                oJson["origin"] = m_strOrigin;
                oJson["title"] = oTask.m_strTitle;
                oJson["account"] = oTask.m_strAccount;
                oJson["novel"] = oTask.m_nNovelId;
                oJson["destination"] = oTask.m_strDestination;
                oJson["location"] = oTask.m_strLocation;
                oJson["message"] = oTask.m_strMessage;
                oJson["local"] = oTask.m_bLocal;
                oJson["images"] = oTask.m_bImages;
                oJson["state"] = int(oTask.m_eState);
                oJson["chaptersCompleted"] = double(oTask.m_nChaptersCompleted);
                oJson["chaptersTotal"] = double(oTask.m_nChaptersTotal);
                oJson["imagesCompleted"] = double(oTask.m_nImagesCompleted);
                oJson["imagesTotal"] = double(oTask.m_nImagesTotal);
                vTasks.append(oJson);
            }
            const QByteArray baJson = QJsonDocument(QJsonObject{{"version", 1}, {"tasks", vTasks}}).toJson();
            if (!QDir().mkpath(m_strRoot))
            {
                return false;
            }
            QSaveFile oFile(QDir(m_strRoot).filePath("tasks.json"));
            oFile.setDirectWriteFallback(false);
            return oFile.open(QIODevice::WriteOnly) && oFile.write(baJson) == baJson.size() && oFile.commit();
        }
        void load()
        {
            QFile oFile(QDir(m_strRoot).filePath("tasks.json"));
            if (!oFile.exists())
            {
                return;
            }
            if (!oFile.open(QIODevice::ReadOnly) || oFile.size() > 16 * 1024 * 1024)
            {
                warn("无法读取下载任务记录，请检查应用存储空间。");
                return;
            }
            QJsonParseError oError;
            const auto oDocument = QJsonDocument::fromJson(oFile.readAll(), &oError);
            if (oError.error != QJsonParseError::NoError || !oDocument.isObject() ||
                oDocument.object().value("version").toInt() != 1)
            {
                warn("下载任务记录格式无效，原文件已保留。");
                return;
            }
            for (const auto& oValue : oDocument.object().value("tasks").toArray())
            {
                const auto oJson = oValue.toObject();
                DownloadTask oTask;
                oTask.m_strId = oJson.value("id").toString();
                oTask.m_nNovelId = oJson.value("novel").toInt();
                if (!validId(oTask.m_strId) || find(oTask.m_strId) || oTask.m_nNovelId <= 0 ||
                    oJson.value("origin").toString() != m_strOrigin)
                {
                    continue;
                }
                oTask.m_strTitle = oJson.value("title").toString();
                oTask.m_strAccount = oJson.value("account").toString();
                oTask.m_strDestination = oJson.value("destination").toString();
                oTask.m_strLocation = oJson.value("location").toString();
                oTask.m_strMessage = oJson.value("message").toString();
                if (oTask.m_strMessage.contains("拒绝") || oTask.m_strMessage.contains("登录验证后"))
                {
                    oTask.m_strMessage = "网络访问失败，进度已保留，可稍后继续。";
                }
                oTask.m_bLocal = oJson.value("local").toBool();
                oTask.m_bImages = oJson.value("images").toBool(true);
                const int nState = oJson.value("state").toInt(-1);
                if (nState < 0 || nState > int(DownloadTaskState::Complete) ||
                    oTask.m_bLocal != !oTask.m_strDestination.isEmpty() || oTask.m_strAccount.isEmpty())
                {
                    continue;
                }
                oTask.m_eState = DownloadTaskState(nState);
                if (oTask.m_eState == DownloadTaskState::Running)
                {
                    oTask.m_eState = DownloadTaskState::Queued;
                }
                if (!oTask.m_strLocation.isEmpty() && oTask.m_bLocal)
                {
                    oTask.m_eState = DownloadTaskState::Complete;
                }
                oTask.m_nChaptersCompleted = qMax<qint64>(0, qint64(oJson.value("chaptersCompleted").toDouble()));
                oTask.m_nChaptersTotal = qMax<qint64>(0, qint64(oJson.value("chaptersTotal").toDouble()));
                oTask.m_nImagesCompleted = qMax<qint64>(0, qint64(oJson.value("imagesCompleted").toDouble()));
                oTask.m_nImagesTotal = qMax<qint64>(0, qint64(oJson.value("imagesTotal").toDouble()));
                m_vTasks.append(oTask);
            }
        }
        bool progress(const QString& strId, const std::shared_ptr<std::atomic_bool>& pCancel,
                      const DownloadStatus& oStatus, const QString& strMessage = {})
        {
            if (pCancel->load())
            {
                return false;
            }
            std::lock_guard<std::mutex> oLock(m_oMutex);
            auto* pTask = find(strId);
            if (!pTask || pTask->m_eState != DownloadTaskState::Running || m_bStopped.load())
            {
                return false;
            }
            const bool bNew = (!strMessage.isEmpty() && pTask->m_strMessage != strMessage) ||
                              pTask->m_nChaptersCompleted != qint64(oStatus.m_nChaptersCompleted) ||
                              pTask->m_nImagesCompleted != qint64(oStatus.m_nImagesCompleted) ||
                              pTask->m_nChaptersTotal != qint64(oStatus.m_nChaptersTotal) ||
                              pTask->m_nImagesTotal != qint64(oStatus.m_nImagesTotal);
            if (bNew)
            {
                if (!strMessage.isEmpty())
                {
                    pTask->m_strMessage = strMessage;
                }
                pTask->m_nChaptersCompleted = qint64(oStatus.m_nChaptersCompleted);
                pTask->m_nChaptersTotal = qint64(oStatus.m_nChaptersTotal);
                pTask->m_nImagesCompleted = qint64(oStatus.m_nImagesCompleted);
                pTask->m_nImagesTotal = qint64(oStatus.m_nImagesTotal);
                if (!save())
                {
                    pTask->m_eState = DownloadTaskState::Failed;
                    pTask->m_strMessage = "无法保存下载进度，请检查存储空间后继续。";
                    pCancel->store(true);
                    warn(pTask->m_strMessage);
                    changed();
                    return false;
                }
                // Keep reads current even when the next UI notification is throttled.
                std::lock_guard<std::mutex> oSnapshotLock(m_oSnapshotMutex);
                m_vSnapshot = m_vTasks;
            }
            const auto oNow = std::chrono::steady_clock::now();
            if (bNew && oNow - m_oLastNotify >= std::chrono::milliseconds(250))
            {
                m_oLastNotify = oNow;
                changed();
            }
            return !pCancel->load();
        }
        QString execute(const DownloadTask& oTask, ClientOptions oOptions,
                        const std::shared_ptr<HttpTransport>& pTransport,
                        const std::shared_ptr<std::atomic_bool>& pCancel)
        {
            const QString strWork = QDir(m_strRoot).filePath(oTask.m_strId);
            oOptions.m_strEndpoint = m_strOrigin.toStdString();
            oOptions.m_oCacheDirectory = nativePath(QDir(strWork).filePath("http"));
            oOptions.m_oTransfer.m_fnProgress = [pCancel](auto, auto)
            {
                return !pCancel->load();
            };
            DownloadStoreOptions oStore;
            oStore.m_oDirectory = nativePath(oTask.m_bLocal ? QDir(strWork).filePath("source") : m_strCache);
            oStore.m_vImageOrigins.push_back("https://pic.777743.xyz");
            oStore.m_oAssets.m_fnProgress = [pCancel](auto, auto)
            {
                return !pCancel->load();
            };
            auto oRetry = clientRetryOptions(true, false);
            oRetry.m_oDelay = std::chrono::seconds(2);
            NovelDownloader oDownloader(oOptions, oStore, makeRetryingTransport(pTransport, oRetry),
                makeRetryingTransport(makeClientAssetTransport(), clientAssetRetryOptions(false)));
            DownloadRequest oRequest;
            oRequest.m_eMode = DownloadMode::Resume;
            oRequest.m_bIncludeImages = oTask.m_bImages;
            oRequest.m_bIncludeCover = oTask.m_bImages;
            oRequest.m_nRateLimitRetries = 0;
            oRequest.m_fnProgress = [this, oTask, pCancel](const DownloadStatus& oStatus)
            {
                return progress(oTask.m_strId, pCancel, oStatus, "正在下载…");
            };
            if (!oTask.m_bLocal)
            {
                oDownloader.download(oTask.m_nNovelId, oRequest);
                return {};
            }
            if (!exportDirectoryAvailable(oTask.m_strDestination))
            {
                throw Error(ErrorCode::Io, "本地保存位置不可用，请重新选择目录后下载。");
            }
            // Only discard regenerated public files; source checkpoints survive retries.
            const QString strPortable = QDir(strWork).filePath("portable");
            if (QDir(strPortable).exists() && !QDir(strPortable).removeRecursively())
            {
                throw Error(ErrorCode::Io, "无法清理本任务的临时导出文件。");
            }
            ExportOptions oExport;
            oExport.m_oDirectory = nativePath(QDir(strWork).filePath("portable"));
            oExport.m_bIncludeImages = oTask.m_bImages;
            oExport.m_fnProgress = [this, oTask, pCancel](const ExportProgress& oExportStatus)
            {
                DownloadStatus oStatus;
                oStatus.m_nChaptersCompleted = oExportStatus.m_nChaptersCompleted;
                oStatus.m_nChaptersTotal = oExportStatus.m_nChaptersTotal;
                oStatus.m_nImagesCompleted = oExportStatus.m_nImagesCompleted;
                oStatus.m_nImagesTotal = oExportStatus.m_nImagesTotal;
                return progress(oTask.m_strId, pCancel, oStatus, "正在整理文件…");
            };
            ExportResult oExported;
            bool bFromCache = false;
            DownloadStoreOptions oCachedOptions;
            oCachedOptions.m_oDirectory = nativePath(m_strCache);
            NovelDownloader oCached(oOptions, oCachedOptions, pTransport);
            try
            {
                const auto oCachedStatus = oCached.status(oTask.m_nNovelId);
                if (oCachedStatus.m_bHasReadableVersion)
                {
                    oExported = exportNovel(oCached, oTask.m_nNovelId, oExport);
                    bFromCache = true;

                }
            }
            catch (const Error& oError)
            {
                if (oError.code() != ErrorCode::CacheMiss)
                {
                    throw;
                }
            }
            if (!bFromCache)
            {
                oDownloader.download(oTask.m_nNovelId, oRequest);
                oExported = exportNovel(oDownloader, oTask.m_nNovelId, oExport);
            }
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                if (auto* pTask = find(oTask.m_strId))
                {
                    pTask->m_strMessage = "正在保存到所选目录…";
                    if (!save())
                    {
                        throw Error(ErrorCode::Io, "Cannot persist export task");
                    }
                    changed();
                }
            }
            // The publish operation checks cancellation until its commit boundary.
            // After it returns, its output must be recorded even if pause races it.
            return publishExportDirectory(QString::fromStdString(oExported.m_oDirectory.u8string()),
                                          oTask.m_strDestination,
                                          [pCancel]
                                          {
                                              return !pCancel->load();
                                          });
        }
        void work()
        {
            for (;;)
            {
                DownloadTask oTask;
                ClientOptions oOptions;
                std::shared_ptr<HttpTransport> pTransport;
                auto pCancel = std::make_shared<std::atomic_bool>(false);
                {
                    std::unique_lock<std::mutex> oLock(m_oMutex);
                    m_oWake.wait(oLock, [this]
                    {
                        if (m_bStopped.load())
                        {
                            return true;
                        }
                        if (m_bSession)
                        {
                            for (const auto& oEntry : m_vTasks)
                            {
                                if (oEntry.m_eState == DownloadTaskState::Queued &&
                                    oEntry.m_strAccount == m_strAccount)
                                {
                                    return true;
                                }
                            }
                        }
                        return false;
                    });
                    if (m_bStopped.load())
                    {
                        return;
                    }
                    for (auto& oEntry : m_vTasks)
                    {
                        if (oEntry.m_eState == DownloadTaskState::Queued && oEntry.m_strAccount == m_strAccount)
                        {
                            oTask = oEntry;
                            oEntry.m_eState = DownloadTaskState::Running;
                            oEntry.m_strMessage = "正在下载…";
                            break;
                        }
                    }
                    m_strActive = oTask.m_strId;
                    m_pCancel = pCancel;
                    if (!save())
                    {
                        auto* pTask = find(oTask.m_strId);
                        pTask->m_eState = DownloadTaskState::Failed;
                        pTask->m_strMessage = "无法保存下载任务，未开始下载。";
                        m_strActive.clear();
                        warn(pTask->m_strMessage);
                        changed();
                        continue;
                    }
                    oOptions = m_oOptions;
                    pTransport = m_pTransport;
                    changed();
                }
                bool bSuccess = false;
                QString strLocation, strError;
                DownloadTaskState eFailure = DownloadTaskState::Failed;
                try
                {
                    strLocation = execute(oTask, oOptions, pTransport, pCancel);
                    bSuccess = true;
                }
                catch (const Error& oError)
                {
                    if (oError.code() == ErrorCode::Cancelled || oError.code() == ErrorCode::Network ||
                        oError.code() == ErrorCode::Authentication || oError.httpStatus() == 403 ||
                        oError.httpStatus() == 429 || oError.code() == ErrorCode::BrowserVerificationRequired ||
                        oError.code() == ErrorCode::CaptchaRequired)
                    {
                        eFailure = DownloadTaskState::Paused;
                    }
                    strError = oError.code() == ErrorCode::Cancelled ? "下载已暂停，可继续。"
                        : oError.httpStatus() == 403 || oError.code() == ErrorCode::Network ||
                          oError.code() == ErrorCode::BrowserVerificationRequired ||
                          oError.code() == ErrorCode::CaptchaRequired
                            ? "网络访问失败，进度已保留，可稍后继续。"
                        : oError.code() == ErrorCode::Io ? "存储不可用，请检查目录权限和剩余空间后继续。"
                        : "下载未完成，已保留进度，可稍后继续。";
                }
                catch (const std::exception&)
                {
                    strError = "下载未完成，已保留进度，可稍后继续。";
                }
                bool bClean = false;
                {
                    std::lock_guard<std::mutex> oLock(m_oMutex);
                    if (auto* pTask = find(oTask.m_strId))
                    {
                        // A successful commit wins a late pause/shutdown request.
                        if (bSuccess)
                        {
                            pTask->m_eState = DownloadTaskState::Complete;
                            pTask->m_strLocation = strLocation;
                            pTask->m_strMessage = oTask.m_bLocal ? "已保存到本地。" : "下载缓存已完成。";
                            const bool bSaved = save();
                            bClean = bSaved;
                            if (!bSaved)
                            {
                                warn("文件已下载，但任务记录保存失败，请保留文件并检查存储空间。");
                            }
                            post([this, oTask, strLocation]
                            {
                                emit m_pQ->completed(oTask.m_nNovelId, oTask.m_bLocal, strLocation);
                            });
                        }
                        else if (pTask->m_eState == DownloadTaskState::Running)
                        {
                            pTask->m_eState = eFailure;
                            pTask->m_strMessage = strError;
                            if (!save())
                            {
                                warn("下载状态保存失败，请检查存储空间。");
                            }
                        }
                        changed();
                    }
                    else
                    {
                        bClean = true;
                    }
                    m_strActive.clear();
                    m_pCancel.reset();
                }
                if (bClean)
                {
                    // UUID is validated on load and generated internally on enqueue.
                    QDir(QDir(m_strRoot).filePath(oTask.m_strId)).removeRecursively();
                }
            }
        }
    };
    DownloadManager::DownloadManager(const QString& strCacheDirectory, const QString& strOrigin,
                                     QObject* pParent)
        : QObject(pParent), m_pImpl(std::make_unique<Impl>(this, strCacheDirectory, strOrigin))
    {
    }
    DownloadManager::~DownloadManager()
    {
        shutdown();
    }
    QList<DownloadTask> DownloadManager::tasks() const
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oSnapshotMutex);
        return m_pImpl->m_vSnapshot;
    }
    void DownloadManager::setSession(const ClientOptions& oOptions,
                                     std::shared_ptr<HttpTransport> pTransport,
                                     const QString& strAccount)
    {
        if (m_pImpl->m_bStopped.load())
        {
            return;
        }
        suspend();
        {
            std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
            m_pImpl->m_oOptions = oOptions;
            m_pImpl->m_pTransport = std::move(pTransport);
            m_pImpl->m_strAccount = strAccount;
            m_pImpl->m_bSession = !strAccount.isEmpty() && bool(m_pImpl->m_pTransport);
        }
        m_pImpl->m_oWake.notify_one();
    }
    void DownloadManager::suspend()
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
        m_pImpl->m_bSession = false;
        if (m_pImpl->m_pCancel)
        {
            m_pImpl->m_pCancel->store(true);
        }
        if (auto* pTask = m_pImpl->find(m_pImpl->m_strActive))
        {
            if (pTask->m_eState == DownloadTaskState::Running)
            {
                pTask->m_eState = DownloadTaskState::Queued;
                pTask->m_strMessage = "等待验证登录状态后继续。";
                m_pImpl->save();
                m_pImpl->changed();
            }
        }
    }
    void DownloadManager::shutdown()
    {
        if (m_pImpl->m_bStopped.exchange(true))
        {
            return;
        }
        suspend();
        m_pImpl->m_oWake.notify_one();
        while (!m_pImpl->m_bFinished.load())
        {
            // Browser transports may have a queued GUI-thread request to cancel.
            if (QThread::currentThread() == thread())
            {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        m_pImpl->m_oWorker.join();
    }
    QString DownloadManager::enqueue(int nId, const QString& strTitle, bool bImages,
                                     const QString& strDestination)
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
        if (m_pImpl->m_bStopped.load() || !m_pImpl->m_bSession || nId <= 0)
        {
            return {};
        }
        for (const auto& oTask : m_pImpl->m_vTasks)
        {
            if (oTask.m_nNovelId == nId && oTask.m_strAccount == m_pImpl->m_strAccount &&
                oTask.m_bLocal == !strDestination.isEmpty() && oTask.m_strDestination == strDestination &&
                oTask.m_eState != DownloadTaskState::Complete)
            {
                return oTask.m_strId;
            }
        }
        DownloadTask oTask;
        oTask.m_strId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        oTask.m_nNovelId = nId;
        oTask.m_strTitle = strTitle;
        oTask.m_strAccount = m_pImpl->m_strAccount;
        oTask.m_bImages = bImages;
        oTask.m_strDestination = strDestination;
        oTask.m_bLocal = !strDestination.isEmpty();
        oTask.m_strMessage = "等待下载…";
        m_pImpl->m_vTasks.append(oTask);
        if (!m_pImpl->save())
        {
            m_pImpl->m_vTasks.removeLast();
            m_pImpl->warn("无法保存下载任务，未开始下载，请检查存储空间。");
            return {};
        }
        m_pImpl->changed();
        m_pImpl->m_oWake.notify_one();
        return oTask.m_strId;
    }
    void DownloadManager::pause(const QString& strId)
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
        auto* pTask = m_pImpl->find(strId);
        if (!pTask || m_pImpl->m_bStopped.load() || pTask->m_eState == DownloadTaskState::Complete)
        {
            return;
        }
        if (strId == m_pImpl->m_strActive && m_pImpl->m_pCancel)
        {
            m_pImpl->m_pCancel->store(true);
        }
        pTask->m_eState = DownloadTaskState::Paused;
        pTask->m_strMessage = "下载已暂停，可继续。";
        if (!m_pImpl->save())
        {
            m_pImpl->warn("暂停状态保存失败，请检查存储空间。");
        }
        m_pImpl->changed();
    }
    void DownloadManager::resume(const QString& strId)
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
        auto* pTask = m_pImpl->find(strId);
        if (!pTask || m_pImpl->m_bStopped.load() || pTask->m_eState == DownloadTaskState::Complete ||
            pTask->m_eState == DownloadTaskState::Running)
        {
            return;
        }
        const auto ePrevious = pTask->m_eState;
        pTask->m_eState = DownloadTaskState::Queued;
        pTask->m_strMessage = "等待继续下载…";
        if (!m_pImpl->save())
        {
            pTask->m_eState = ePrevious;
            m_pImpl->warn("无法保存任务状态，未开始继续下载。");
            return;
        }
        m_pImpl->changed();
        m_pImpl->m_oWake.notify_one();
    }
    void DownloadManager::remove(const QString& strId)
    {
        std::lock_guard<std::mutex> oLock(m_pImpl->m_oMutex);
        for (int nIndex = 0; nIndex < m_pImpl->m_vTasks.size(); ++nIndex)
        {
            if (m_pImpl->m_vTasks[nIndex].m_strId == strId)
            {
                const auto oTask = m_pImpl->m_vTasks.takeAt(nIndex);
                if (!m_pImpl->save())
                {
                    m_pImpl->m_vTasks.insert(nIndex, oTask);
                    m_pImpl->warn("无法保存任务列表，未移除任务。");
                    return;
                }
                const bool bActive = strId == m_pImpl->m_strActive;
                if (bActive && m_pImpl->m_pCancel)
                {
                    m_pImpl->m_pCancel->store(true);
                }
                else if (validId(strId))
                {
                    QDir(QDir(m_pImpl->m_strRoot).filePath(strId)).removeRecursively();
                }
                m_pImpl->changed();
                return;
            }
        }
    }
}
