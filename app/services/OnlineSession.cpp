// SPDX-License-Identifier: GPL-2.0-only
#include "ClientNetwork.h"
#include "OnlineSession.h"
#include "Icons.h"
#include "Reader.h"
#include "NetworkRetry.h"
#include "LoginHost.h"
#include "RasterImage.h"
#include "ExportDestination.h"
#include <QTemporaryDir>
#include <QDir>
#include <QCache>
#include <QDateTime>
#include <QSet>
#include <limits>
#include <mutex>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <QMap>
namespace Wenku8::Reader
{
    namespace
    {
        // Failed automatic profile probes must not reset the page while a user types.
        // Only the explicit Switch Account action resets this browser-owned session.
        class BrowserOwnedSession final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pInner;

          public:
            explicit BrowserOwnedSession(std::shared_ptr<HttpTransport> pInner)
                : m_pInner(std::move(pInner))
            {
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                return m_pInner->perform(oRequest, oOptions);
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return m_pInner->supportsSameOriginGetRedirects();
            }
            void clearCookies() override
            {
            }
        };
        // Only foreground jobs use this state, and OnlineSession serializes them.
        // The browser continues to own every cookie; the proxy changes request limits,
        // not credentials, when the already-verified SDK client is reused.
        class ForegroundSessionTransport final : public HttpTransport
        {
            std::shared_ptr<HttpTransport> m_pCurrent;
            TransferOptions m_oLimits;

          public:
            void prepare(std::shared_ptr<HttpTransport> pTransport, const TransferOptions& oLimits)
            {
                m_pCurrent = std::move(pTransport);
                m_oLimits = oLimits;
            }
            HttpResponse perform(const HttpRequest& oRequest,
                                 const TransferOptions& oOptions) override
            {
                auto oEffective = oOptions;
                oEffective.m_oTimeout = m_oLimits.m_oTimeout;
                oEffective.m_oConnectTimeout = m_oLimits.m_oConnectTimeout;
                oEffective.m_fnProgress = m_oLimits.m_fnProgress;
                return m_pCurrent->perform(oRequest, oEffective);
            }
            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return m_pCurrent && m_pCurrent->supportsSameOriginGetRedirects();
            }
            void clearCookies() override
            {
                if (m_pCurrent)
                    m_pCurrent->clearCookies();
            }
        };
        struct ShelfSessionState
        {
            std::shared_ptr<ForegroundSessionTransport> m_pTransport =
                std::make_shared<ForegroundSessionTransport>();
            std::unique_ptr<Client> m_pClient;
            bool m_bVerified = false;
            void prepare(const ClientOptions& oOptions, std::shared_ptr<HttpTransport> pCurrent)
            {
                m_pTransport->prepare(std::move(pCurrent), oOptions.m_oTransfer);
                if (!m_pClient)
                    m_pClient = std::make_unique<Client>(oOptions, m_pTransport);
            }
            UserProfile profile(const ClientOptions& oOptions,
                                std::shared_ptr<HttpTransport> pCurrent)
            {
                prepare(oOptions, std::move(pCurrent));
                m_bVerified = false;
                auto oProfile = m_pClient->userInfo();
                m_bVerified = true;
                return oProfile;
            }
            std::vector<Novel> shelf(const ClientOptions& oOptions,
                                     std::shared_ptr<HttpTransport> pCurrent)
            {
                prepare(oOptions, std::move(pCurrent));
                if (!m_bVerified)
                {
                    m_pClient->userInfo();
                    m_bVerified = true;
                }
                return m_pClient->bookshelf();
            }
        };
        struct ShelfSessionSlot
        {
            std::mutex m_oMutex;
            std::shared_ptr<ShelfSessionState> m_pState = std::make_shared<ShelfSessionState>();
            std::shared_ptr<ShelfSessionState> get()
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                return m_pState;
            }
            void reset()
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                m_pState = std::make_shared<ShelfSessionState>();
            }
        };
        enum class Kind
        {
            Login,
            Novels,
            Book,
            Chapter,
            Download,
            Export,
            ShelfAdd
        };
        struct Result
        {
            std::vector<Novel> m_vNovels;
            Novel m_oNovel;
            std::vector<Volume> m_vVolumes;
            QString m_strText, m_strUsername, m_strLoginHint, m_strStatusOverride, m_strExportedLocation;
            QMap<QString, QByteArray> m_vImages;
            bool m_bSuccess = false, m_bNeedsLogin = false, m_bVerification = false,
                 m_bTransient = false, m_bCancelled = false, m_bFromCache = false, m_bStale = false,
                 m_bIsShelf = false;
            QStringList m_vImageUrls;
            int m_nNovelId = 0, m_nChapterId = 0;
            int m_nId = 0;
        };
        using Job = std::function<Result(ClientOptions, std::shared_ptr<HttpTransport>)>;
        bool readOnly(Kind eKind)
        {
            return eKind == Kind::Novels || eKind == Kind::Book || eKind == Kind::Chapter;
        }
        struct CachedRead
        {
            Result m_oValue;
            qint64 m_nStored = 0;
        };
        struct ImageResult
        {
            QString m_strKey;
            quint64 m_nGeneration = 0;
            int m_nNovel = 0, m_nChapter = 0;
            QMap<QString, QByteArray> m_vImages;
        };
        int cost(const Result& oResult)
        {
            qint64 nBytes = sizeof(Result) + oResult.m_strText.size() * 2;
            auto fnNovelBytes = [](const Novel& oN)
            {
                return qint64(sizeof(Novel) + oN.m_strTitle.size() + oN.m_strAuthor.size() +
                              oN.m_strStatus.size() + oN.m_strIntro.size() +
                              oN.m_strPublisher.size() + oN.m_strLastUpdate.size() +
                              oN.m_strLatestChapter.size());
            };
            nBytes += fnNovelBytes(oResult.m_oNovel);
            for (const auto& oNovel : oResult.m_vNovels)
                nBytes += fnNovelBytes(oNovel);
            for (const auto& oVolume : oResult.m_vVolumes)
            {
                nBytes += sizeof(Volume) + oVolume.m_strTitle.size();
                for (const auto& oChapter : oVolume.m_vChapters)
                    nBytes += sizeof(Chapter) + oChapter.m_strTitle.size();
            }
            for (auto oIt = oResult.m_vImages.cbegin(); oIt != oResult.m_vImages.cend(); ++oIt)
                nBytes += oIt.key().size() * 2 + oIt.value().size();
            return int(qMin<qint64>(std::numeric_limits<int>::max(),
                                    qMax<qint64>(1, (nBytes + 1023) / 1024)));
        }
        Result failure(const Error& oError)
        {
            Result oR;
            oR.m_bCancelled = oError.code() == ErrorCode::Cancelled;
            // A refusal/challenge is not proof that a previously verified account expired.
            oR.m_bNeedsLogin =
                oError.httpStatus() == 401 ||
                (oError.code() == ErrorCode::Authentication && oError.httpStatus() != 403);
            oR.m_bVerification =
                !oR.m_bNeedsLogin && (oError.httpStatus() == 403 ||
                                      oError.code() == ErrorCode::BrowserVerificationRequired ||
                                      oError.code() == ErrorCode::CaptchaRequired);
            oR.m_bTransient = oError.code() == ErrorCode::Network || oError.httpStatus() == 408 ||
                              oError.httpStatus() == 425 || oError.httpStatus() == 429 ||
                              oError.httpStatus() == 500 || oError.httpStatus() == 502 ||
                              oError.httpStatus() == 503 || oError.httpStatus() == 504;
            if (oError.httpStatus() > 0)
                oR.m_strLoginHint = QString("登录检查返回 HTTP %1，请完成网页验证后再检查。")
                                        .arg(oError.httpStatus());
            else if (oError.code() == ErrorCode::Network)
                oR.m_strLoginHint = "登录检查连接失败，请稍后重试。";
            else if (oError.code() == ErrorCode::Authentication ||
                     oError.code() == ErrorCode::Parse)
                oR.m_strLoginHint = "尚未核实到有效账号资料，请完成网页登录。";
            oR.m_strText = oR.m_bNeedsLogin ? "登录状态已过期，请重新登录。"
                           : oR.m_bVerification
                               ? "本次访问被网站拒绝；已保留登录状态，可稍后重试或打开网站验证。"
                           : oR.m_bCancelled ? "操作已取消，已下载的内容会保留。"
                                             : "加载失败，请检查网络后重试。";
            return oR;
        }
        QString assetUrl(const QString& strOriginal)
        {
            QUrl oUrl(strOriginal);
            const auto strHost = oUrl.host().toLower();
            if (strHost != "img.wenku8.com" && strHost != "pic.wenku8.com" &&
                strHost != "pic.777743.xyz")
                return {};
            if (oUrl.scheme() == "http" &&
                (strHost == "img.wenku8.com" || strHost == "pic.wenku8.com"))
                oUrl.setScheme("https");
            if (oUrl.scheme() != "https" || !oUrl.userInfo().isEmpty() ||
                (oUrl.port() != -1 && oUrl.port() != 443))
                return {};
            return QString::fromLatin1(oUrl.toEncoded());
        }
    } // namespace
    struct OnlineSession::Impl
    {
        OnlineSession* m_pQ;
        std::unique_ptr<LoginHost> m_pHost;
        QTimer m_oProbeTimer, m_oRestoreDeadline;
        QFutureWatcher<Result> m_oWatcher;
        QFutureWatcher<ImageResult> m_oImageWatcher;
        QCache<QString, CachedRead> m_vReads{32 * 1024}; // KiB, verified-account memory only.
        QString m_strCurrentKey, m_strPendingKey, m_strDataKey, m_strImagePendingKey;
        Result m_oImagePending;
        bool m_bCurrentFresh = false, m_bPendingFresh = false;
        Job m_fnFailedJob;
        Kind m_eFailedKind = Kind::Novels;
        QString m_strFailedKey;
        quint64 m_nImageGeneration = 0;
        std::shared_ptr<std::atomic_bool> m_pImageCancelled =
            std::make_shared<std::atomic_bool>(false);
        QString m_strOrigin, m_strDirectory, m_strUser;
        bool m_bRestoringSession = false, m_bAuthenticated = false, m_bActive = false,
             m_bShuttingDown = false, m_bOpenAfterTask = false, m_bResumed = false;
        Kind m_eKind = Kind::Login, m_ePendingKind = Kind::Novels;
        Job m_fnCurrent, m_fnPending;
        std::shared_ptr<ShelfSessionSlot> m_pShelfSession = std::make_shared<ShelfSessionSlot>();
        Result m_oData;
        std::shared_ptr<std::atomic_bool> m_pCancelled = std::make_shared<std::atomic_bool>(false);
        Impl(OnlineSession* pOwner, QWidget* pWindow, QString strSite, QString strStore,
             const QString& strStorage)
            : m_pQ(pOwner), m_pHost(makeLoginHost(pWindow, QUrl(strSite), strStorage)),
              m_strOrigin(std::move(strSite)), m_strDirectory(std::move(strStore))
        {
            QObject::connect(m_pHost.get(), &LoginHost::checkRequested, m_pQ,
                             [this]
                             {
                                 probe();
                             });
            QObject::connect(
                m_pHost.get(), &LoginHost::unavailable, m_pQ,
                [this]
                {
                    setRestoring(false);
                    m_bAuthenticated = false;
                    m_pCancelled->store(true);
                    m_oProbeTimer.stop();
                    clearReads();
                    emit m_pQ->authenticationChanged(false);
                    emit m_pQ->statusChanged(
                        "系统网页组件不可用，请更新 Android System WebView 并重启客户端。");
                });
            m_oRestoreDeadline.setSingleShot(true);
            m_oRestoreDeadline.setInterval(45000);
            QObject::connect(&m_oRestoreDeadline, &QTimer::timeout, m_pQ,
                             [this]
                             {
                                 if (!m_bRestoringSession)
                                     return;
                                 m_pCancelled->store(true);
                                 setRestoring(false);
                                 m_oProbeTimer.stop();
                                 emit m_pQ->statusChanged(
                                     "登录状态检查超时，请检查网络后点击登录文库重试。");
                             });
            m_oProbeTimer.setInterval(2500);
            QObject::connect(&m_oProbeTimer, &QTimer::timeout, m_pQ,
                             [this]
                             {
                                 probe();
                             });
            QObject::connect(m_pHost.get(), &LoginHost::pageLoaded, m_pQ,
                             [this]()
                             {
                                 if (m_pHost->visible() || m_bRestoringSession)
                                     QTimer::singleShot(400, m_pQ,
                                                        [this]
                                                        {
                                                            probe();
                                                        });
                             });
            QObject::connect(m_pHost.get(), &LoginHost::cancelled, m_pQ,
                             [this]
                             {
                                 setRestoring(false);
                                 m_oProbeTimer.stop();
                                 if (!m_bAuthenticated)
                                 {
                                     m_pCancelled->store(true);
                                     emit m_pQ->statusChanged("尚未登录，点击「登录文库」继续。");
                                 }
                             });
            QObject::connect(&m_oWatcher, &QFutureWatcher<Result>::finished, m_pQ,
                             [this]
                             {
                                 finish(m_oWatcher.result());
                             });
            QObject::connect(
                &m_oImageWatcher, &QFutureWatcher<ImageResult>::finished, m_pQ,
                [this]
                {
                    if (m_bShuttingDown)
                        return;
                    const auto oResult = m_oImageWatcher.result();
                    if (oResult.m_nGeneration == m_nImageGeneration && !m_pImageCancelled->load())
                    {
                        if (auto pPrevious = m_vReads.object(oResult.m_strKey))
                        {
                            auto oSaved = pPrevious->m_oValue;
                            for (auto oIt = oResult.m_vImages.cbegin();
                                 oIt != oResult.m_vImages.cend(); ++oIt)
                                oSaved.m_vImages.insert(oIt.key(), oIt.value());
                            remember(oResult.m_strKey, oSaved, pPrevious->m_nStored);
                        }
                        if (m_strDataKey == oResult.m_strKey)
                        {
                            for (auto oIt = oResult.m_vImages.cbegin();
                                 oIt != oResult.m_vImages.cend(); ++oIt)
                                m_oData.m_vImages.insert(oIt.key(), oIt.value());
                            if (!oResult.m_vImages.isEmpty())
                                emit m_pQ->chapterImagesReady(oResult.m_nNovel, oResult.m_nChapter);
                        }
                    }
                    startImages();
                });
        }
        ~Impl()
        {
            m_bShuttingDown = true;
            m_oProbeTimer.stop();
            m_pCancelled->store(true);
            m_pImageCancelled->store(true);
            m_pHost->close();
            m_oWatcher.waitForFinished();
            m_oImageWatcher.waitForFinished();
        }
        void clearRetry()
        {
            if (m_fnFailedJob)
            {
                m_fnFailedJob = {};
                m_strFailedKey.clear();
                emit m_pQ->retryAvailableChanged(false);
            }
        }
        void clearReads(bool bPreserveVerifiedClient = false)
        {
            if (!bPreserveVerifiedClient)
                m_pShelfSession->reset();
            m_pImageCancelled->store(true);
            ++m_nImageGeneration;
            m_strImagePendingKey.clear();
            m_oImagePending = {};
            m_vReads.clear();
            m_oData = {};
            m_strDataKey.clear();
            clearRetry();
        }
        void remember(const QString& strKey, const Result& oResult, qint64 nWhen = 0)
        {
            if (!strKey.isEmpty())
                m_vReads.insert(
                    strKey,
                    new CachedRead{oResult, nWhen ? nWhen : QDateTime::currentMSecsSinceEpoch()},
                    cost(oResult));
        }
        void scheduleImages()
        {
            m_pImageCancelled->store(true);
            ++m_nImageGeneration;
            m_strImagePendingKey = m_strDataKey;
            m_oImagePending = m_oData;
            startImages();
        }
        void startImages()
        {
            if (m_bShuttingDown || m_oImageWatcher.isRunning() || m_strImagePendingKey.isEmpty())
                return;
            const auto strKey = m_strImagePendingKey;
            m_strImagePendingKey.clear();
            const auto oChapter = m_oImagePending;
            m_oImagePending = {};
            QStringList vMissing;
            QSet<QString> vSeen;
            qint64 nUsed = 0;
            for (const auto& baBytes : oChapter.m_vImages)
                nUsed += baBytes.size();
            for (const auto& strOriginal : oChapter.m_vImageUrls)
            {
                if (vMissing.size() >= 64)
                    break;
                if (!vSeen.contains(strOriginal) && !oChapter.m_vImages.contains(strOriginal) &&
                    !assetUrl(strOriginal).isEmpty())
                {
                    vSeen.insert(strOriginal);
                    vMissing.append(strOriginal);
                }
            }
            if (vMissing.isEmpty() || nUsed >= 32 * 1024 * 1024)
                return;
            const auto nGeneration = m_nImageGeneration;
            auto pFlag = std::make_shared<std::atomic_bool>(false);
            m_pImageCancelled = pFlag;
            m_oImageWatcher.setFuture(QtConcurrent::run(
                [strKey, oChapter, vMissing, nGeneration, pFlag, nUsed]() mutable
                {
                    ImageResult oResult;
                    oResult.m_strKey = strKey;
                    oResult.m_nGeneration = nGeneration;
                    oResult.m_nNovel = oChapter.m_nNovelId;
                    oResult.m_nChapter = oChapter.m_nChapterId;
                    try
                    {
                        auto oRetry = clientAssetRetryOptions();
                        oRetry.m_oAttemptTimeout = std::chrono::seconds(6);
                        auto pAssets = makeRetryingTransport(makeClientAssetTransport(), oRetry);
                        const auto oDeadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(20);
                        for (const auto& strOriginal : vMissing)
                        {
                            if (pFlag->load() || nUsed >= 32 * 1024 * 1024)
                                break;
                            const auto oRemaining =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    oDeadline - std::chrono::steady_clock::now());
                            if (oRemaining.count() <= 0)
                                break;
                            TransferOptions oLimits;
                            oLimits.m_oTimeout = qMin(oRemaining, std::chrono::milliseconds(10000));
                            oLimits.m_oConnectTimeout = std::chrono::seconds(5);
                            oLimits.m_bFollowRedirects = false;
                            oLimits.m_nMaxResponseBytes =
                                size_t(qMin<qint64>(8 * 1024 * 1024, 32 * 1024 * 1024 - nUsed));
                            oLimits.m_fnProgress = [pFlag](auto, auto)
                            {
                                return !pFlag->load();
                            };
                            try
                            {
                                auto oResponse = pAssets->perform(
                                    {assetUrl(strOriginal).toStdString(), "GET", {}, {}}, oLimits);
                                QByteArray baBytes(oResponse.m_strBody.data(),
                                                   int(oResponse.m_strBody.size()));
                                if (oResponse.m_nStatus == 200 && !rasterFormat(baBytes).isEmpty())
                                {
                                    nUsed += baBytes.size();
                                    oResult.m_vImages.insert(strOriginal, baBytes);
                                }
                            }
                            catch (const std::exception&)
                            {
                                if (pFlag->load())
                                    break;
                            }
                        }
                    }
                    catch (const std::exception&)
                    {
                    } // An image transport failure must not fail the chapter or escape through
                      // QFuture.
                    return oResult;
                }));
        }
        void setRestoring(bool bValue)
        {
            if (m_bRestoringSession == bValue)
                return;
            m_bRestoringSession = bValue;
            if (bValue)
                m_oRestoreDeadline.start();
            else
                m_oRestoreDeadline.stop();
            emit m_pQ->restorationChanged(bValue);
        }
        void showLogin(bool bReset = false)
        {
            if (m_bActive)
            {
                m_bOpenAfterTask = true;
                m_pCancelled->store(true);
                return;
            }
            clearReads();
            m_bAuthenticated = false;
            m_strUser.clear();
            emit m_pQ->authenticationChanged(false);

            setRestoring(false);
            m_pHost->setHint(m_pHost->persistent() ? "此设备会记住登录会话；密码不会保存在客户端。"
                                                   : "当前为临时会话；密码不会保存在客户端。若其他"
                                                     "窗口占用登录状态，请先关闭旧窗口。");
            m_pHost->show(bReset);
            m_oProbeTimer.start();
        }
        void probe()
        {
            if (m_bActive || (!m_pHost->visible() && !m_bRestoringSession) || m_bAuthenticated ||
                !m_pHost->hasPage())
                return;
            const auto pVerifiedSession = m_pShelfSession;
            run(Kind::Login,
                [pVerifiedSession](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
                {
                    auto oProfile = pVerifiedSession->get()->profile(oO, std::move(pT));
                    if (oProfile.m_strUsername.empty())
                        throw Error(ErrorCode::Authentication, "No authenticated account");
                    Result oR;
                    oR.m_bSuccess = true;
                    oR.m_strUsername = QString::fromUtf8(oProfile.m_strUsername.c_str());
                    return oR;
                });
        }
        void run(Kind eType, Job fnJob, bool bReplay = false, QString strKey = {},
                 bool bFresh = false)
        {
            if (m_bActive || m_bShuttingDown)
                return;
            if (eType != Kind::Login && !m_bAuthenticated)
            {
                m_ePendingKind = eType;
                m_fnPending = std::move(fnJob);
                m_strPendingKey = strKey;
                m_bPendingFresh = bFresh;
                showLogin();
                return;
            }
            m_bActive = true;
            m_eKind = eType;
            m_fnCurrent = fnJob;
            m_strCurrentKey = strKey;
            m_bCurrentFresh = bFresh;
            m_bResumed = bReplay;
            m_pCancelled->store(false);
            if (eType != Kind::Login)
                clearRetry();
            if (eType != Kind::Login)
            {
                emit m_pQ->busyChanged(true);
                emit m_pQ->progressChanged(0, 0);
                emit m_pQ->statusChanged(eType == Kind::Book       ? "正在加载小说与目录…"
                                         : eType == Kind::Chapter  ? "正在加载章节内容…"
                                         : eType == Kind::Download ? "正在准备下载缓存…"
                                         : eType == Kind::Export   ? "正在准备下载到本地…"
                                         : eType == Kind::ShelfAdd ? "正在加入书架…"
                                                                   : "正在加载小说列表…");
            }
            if (readOnly(eType) && bFresh)
            {
                if (auto pSaved = m_vReads.object(strKey))
                {
                    const auto nTtl = eType == Kind::Chapter ? 30 * 60 * 1000
                                      : eType == Kind::Book  ? 5 * 60 * 1000
                                                             : 30 * 1000;
                    if (QDateTime::currentMSecsSinceEpoch() - pSaved->m_nStored < nTtl)
                    {
                        auto oResult = pSaved->m_oValue;
                        oResult.m_bFromCache = true;
                        QTimer::singleShot(0, m_pQ,
                                           [this, oResult]
                                           {
                                               if (!m_bShuttingDown)
                                                   finish(oResult);
                                           });
                        return;
                    }
                }
            }
            ClientOptions oOptions;
            oOptions.m_strEndpoint = m_strOrigin.toStdString();
            oOptions.m_eProtocol = Protocol::Website;
            oOptions.m_strUserAgent = m_pHost->userAgent();
            oOptions.m_oTransfer.m_oTimeout = std::chrono::seconds(eType == Kind::Login ? 8 : 45);
            oOptions.m_oTransfer.m_fnProgress = [pFlag = m_pCancelled](auto, auto)
            {
                return !pFlag->load();
            };
            auto oRetry = clientRetryOptions(true, (eType != Kind::Download && eType != Kind::Export));
            if (eType == Kind::Download || eType == Kind::Export)
                oRetry.m_oDelay = std::chrono::seconds(2);
            oRetry.m_fnOnRetry = [this, eType](unsigned nAttempt, int)
            {
                if (eType != Kind::Login)
                    QMetaObject::invokeMethod(
                        m_pQ,
                        [this, nAttempt]
                        {
                            emit m_pQ->statusChanged(
                                QString("网络波动，正在进行第 %1/3 次尝试…").arg(nAttempt));
                        },
                        Qt::QueuedConnection);
            };
            std::shared_ptr<HttpTransport> pTransport = makeRetryingTransport(
                std::make_shared<BrowserOwnedSession>(m_pHost->transport()), oRetry);
            if (eType != Kind::Download && eType != Kind::Export)
                pTransport = makeClientDeadlineTransport(
                    std::move(pTransport), std::chrono::seconds(eType == Kind::Login ? 8 : 30));
            m_oWatcher.setFuture(QtConcurrent::run(
                [fnJob, oOptions, pTransport, eType]
                {
                    try
                    {
                        return fnJob(oOptions, pTransport);
                    }
                    catch (const Error& oError)
                    {
                        auto oResult = failure(oError);
                        if (eType == Kind::Export && oError.code() == ErrorCode::Io)
                        {
                            oResult.m_strText = "保存到本地失败，请检查保存位置的权限和剩余空间，或重新选择目录。";
                        }
                        return oResult;
                    }
                    catch (const std::exception&)
                    {
                        Result oR;
                        oR.m_strText = "暂时无法读取数据，请检查网络或稍后重试。";
                        return oR;
                    }
                }));
        }
        void finish(Result oResult)
        {
            if (m_bShuttingDown)
                return;
            if (m_eKind != Kind::Login && m_pCancelled->load() &&
                !(m_eKind == Kind::Export && oResult.m_bSuccess))
            {
                oResult = {};
                oResult.m_bCancelled = true;
                oResult.m_strText = m_eKind == Kind::Export
                    ? "已取消下载到本地，本次临时文件会清理。"
                    : "操作已取消，已下载的内容会保留。";
            }
            m_bActive = false;
            const bool bBusiness = m_eKind != Kind::Login;
            if (m_bOpenAfterTask)
            {
                m_bOpenAfterTask = false;
                showLogin(true);
                if (bBusiness)
                    emit m_pQ->busyChanged(false);
                return;
            }
            if (m_eKind == Kind::Login)
            {
                // Closing/cancelling a login must never unlock the application from a late reply.
                if ((!m_pHost->visible() && !m_bRestoringSession) || m_pCancelled->load())
                    return;
                if (!oResult.m_bSuccess)
                {
                    if (m_bRestoringSession)
                    {
                        setRestoring(false);
                        m_oProbeTimer.stop();
                        if (oResult.m_bNeedsLogin || oResult.m_bVerification)
                            showLogin();
                        else
                            emit m_pQ->statusChanged(
                                "暂时无法连接；本机登录状态已保留，点击登录文库可重试。");
                    }
                    else
                        m_pHost->setHint(oResult.m_strLoginHint.isEmpty()
                                             ? "等待完成网页登录；客户端会自动检查登录状态。"
                                             : oResult.m_strLoginHint);
                    return;
                }
                if (m_strUser != oResult.m_strUsername)
                    clearReads(true);
                m_bAuthenticated = true;
                m_strUser = oResult.m_strUsername;
                m_oProbeTimer.stop();
                // Publish the verified account and unlock the Qt shell before the
                // native login overlay returns input to it on Android's UI thread.
                emit m_pQ->authenticationChanged(true);
                setRestoring(false);
                if (m_pHost->visible())
                    m_pHost->accept();
                emit m_pQ->statusChanged("已登录，欢迎回来。");
                if (m_fnPending)
                {
                    auto fnJob = std::move(m_fnPending);
                    m_fnPending = {};
                    run(m_ePendingKind, std::move(fnJob), true, m_strPendingKey, m_bPendingFresh);
                }
                return;
            }
            if (!oResult.m_bSuccess && readOnly(m_eKind) && !oResult.m_bCancelled)
            {
                m_fnFailedJob = m_fnCurrent;
                m_eFailedKind = m_eKind;
                m_strFailedKey = m_strCurrentKey;
                emit m_pQ->retryAvailableChanged(true);
                if (oResult.m_bTransient)
                {
                    if (auto pSaved = m_vReads.object(m_strCurrentKey))
                    {
                        auto oCached = pSaved->m_oValue;
                        oCached.m_bFromCache = true;
                        oCached.m_bStale = true;
                        oCached.m_strStatusOverride =
                            "网络暂不可用，正在显示本次会话中上次成功读取的内容；可重试更新。";
                        oResult = std::move(oCached);
                    }
                }
            }
            if (!oResult.m_bSuccess)
            {
                emit m_pQ->statusChanged(oResult.m_strText);
                if (oResult.m_bNeedsLogin)
                {
                    clearReads();
                    m_bAuthenticated = false;
                    emit m_pQ->authenticationChanged(false);
                    if (!m_bResumed)
                    {
                        if (readOnly(m_eKind))
                        {
                            m_fnPending = m_fnCurrent;
                            m_ePendingKind = m_eKind;
                            m_strPendingKey = m_strCurrentKey;
                            m_bPendingFresh = m_bCurrentFresh;
                        }
                        showLogin();
                    }
                }
                else if (oResult.m_bVerification)
                    emit m_pQ->verificationRequired();
                if (bBusiness)
                    emit m_pQ->busyChanged(false);
                return;
            }
            if (readOnly(m_eKind) && !oResult.m_bFromCache)
                remember(m_strCurrentKey, oResult);
            if (m_eKind == Kind::ShelfAdd)
                m_vReads.remove("shelf");
            m_oData = std::move(oResult);
            m_strDataKey = m_strCurrentKey;
            const auto strCompletedKey = m_strDataKey;
            const bool bCachedResult = m_oData.m_bFromCache;
            const auto strCompletedStatus = m_oData.m_bStale       ? m_oData.m_strStatusOverride
                                            : m_oData.m_bFromCache ? QString("已载入本次会话缓存。")
                                            : m_eKind == Kind::Chapter    ? QString("章节已加载")
                                            : m_oData.m_strText.isEmpty() ? QString("已更新")
                                                                          : m_oData.m_strText;
            emit m_pQ->statusChanged(strCompletedStatus);
            switch (m_eKind)
            {
            case Kind::Novels:
                if (m_oData.m_bIsShelf)
                    emit m_pQ->bookshelfReady();
                emit m_pQ->novelsReady();
                break;
            case Kind::Book:
                emit m_pQ->bookReady();
                break;
            case Kind::Chapter:
                emit m_pQ->chapterReady();
                scheduleImages();
                break;
            case Kind::Download:
                emit m_pQ->downloadReady(m_oData.m_nId);
                break;
            case Kind::Export:
                emit m_pQ->localDownloadReady(m_oData.m_nId, m_oData.m_strExportedLocation);
                break;
            case Kind::ShelfAdd:
                emit m_pQ->bookshelfAdded(m_oData.m_nId);
                break;
            default:
                break;
            }
            // UI model slots may emit their own normal status while rendering. Keep
            // stale/cache provenance visible, but never overwrite a follow-on request.
            if (bCachedResult && !m_bActive && m_strDataKey == strCompletedKey)
                emit m_pQ->statusChanged(strCompletedStatus);
            // Keep the old page blocked until its replacement has been committed.
            if (bBusiness && !m_bActive)
                emit m_pQ->busyChanged(false);
        }
    };
    OnlineSession::OnlineSession(QWidget* pOwner, const QString& strOrigin,
                                 const QString& strDirectory, const QString& strStorage)
        : QObject(pOwner), m_pImpl(new Impl(this, pOwner, strOrigin, strDirectory, strStorage))
    {
    }
    OnlineSession::~OnlineSession() = default;
    bool OnlineSession::authenticated() const
    {
        return m_pImpl->m_bAuthenticated;
    }
    bool OnlineSession::busy() const
    {
        return m_pImpl->m_bActive;
    }
    bool OnlineSession::loginVisible() const
    {
        return m_pImpl->m_pHost->visible();
    }
    QString OnlineSession::username() const
    {
        return m_pImpl->m_strUser;
    }
    const std::vector<Novel>& OnlineSession::novels() const
    {
        return m_pImpl->m_oData.m_vNovels;
    }
    const Novel& OnlineSession::novel() const
    {
        return m_pImpl->m_oData.m_oNovel;
    }
    const std::vector<Volume>& OnlineSession::volumes() const
    {
        return m_pImpl->m_oData.m_vVolumes;
    }
    QString OnlineSession::chapterText() const
    {
        return m_pImpl->m_oData.m_strText;
    }
    QByteArray OnlineSession::imageBytes(const QString& strUrl) const
    {
        return m_pImpl->m_oData.m_vImages.value(strUrl);
    }
    void OnlineSession::signIn(bool bReset)
    {
        m_pImpl->showLogin(bReset);
    }
    bool OnlineSession::persistent() const
    {
        return m_pImpl->m_pHost->persistent();
    }
    void OnlineSession::restoreSession()
    {
        if (m_pImpl->m_bActive || m_pImpl->m_bAuthenticated)
            return;
        m_pImpl->setRestoring(true);
        m_pImpl->m_pCancelled->store(false);
        emit statusChanged("正在检查本机登录状态…");
        m_pImpl->m_pHost->openLogin();
        if (m_pImpl->m_bRestoringSession)
            m_pImpl->m_oProbeTimer.start();
    }
    void OnlineSession::setTheme(Theme eTheme)
    {
        m_pImpl->m_pHost->setTheme(eTheme);
    }
    void OnlineSession::cancel()
    {
        m_pImpl->m_pCancelled->store(true);
        m_pImpl->m_pImageCancelled->store(true);
        m_pImpl->clearRetry();
    }
    bool OnlineSession::hasRetryableRequest() const
    {
        return bool(m_pImpl->m_fnFailedJob);
    }
    void OnlineSession::retryLastRequest()
    {
        if (m_pImpl->m_bActive || !m_pImpl->m_bAuthenticated || !m_pImpl->m_fnFailedJob)
            return;
        auto fnJob = m_pImpl->m_fnFailedJob;
        const auto eKind = m_pImpl->m_eFailedKind;
        const auto strKey = m_pImpl->m_strFailedKey;
        m_pImpl->run(eKind, std::move(fnJob), false, strKey,
                     false); // Explicit retry always goes to the network.
    }
    void OnlineSession::shelf(bool bForce)
    {
        const auto pVerifiedSession = m_pImpl->m_pShelfSession;
        m_pImpl->run(
            Kind::Novels,
            [pVerifiedSession](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
            {
                Result oR;
                oR.m_vNovels = pVerifiedSession->get()->shelf(oO, std::move(pT));
                oR.m_bIsShelf = true;
                oR.m_bSuccess = true;
                oR.m_strText = QString("书架已更新 · %1 本小说").arg(oR.m_vNovels.size());
                return oR;
            },
            false, "shelf", !bForce);
    }
    void OnlineSession::latest(int nPage, bool bForce)
    {
        m_pImpl->run(
            Kind::Novels,
            [nPage](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
            {
                Client oC(oO, pT);
                Result oR;
                oR.m_vNovels = oC.list(Sort::LastUpdate, nPage, CachePolicy::NetworkOnly).m_vNovels;
                oR.m_bSuccess = true;
                return oR;
            },
            false, QString("latest:%1").arg(nPage), !bForce);
    }
    void OnlineSession::search(const QString& strQuery, bool bForce)
    {
        if (strQuery.trimmed().isEmpty())
            return;
        m_pImpl->run(
            Kind::Novels,
            [strQuery](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
            {
                Client oC(oO, pT);
                Result oR;
                oR.m_vNovels = oC.search(strQuery.toUtf8().toStdString(), SearchType::Title,
                                         CachePolicy::NetworkOnly)
                                   .m_vNovels;
                oR.m_bSuccess = true;
                return oR;
            },
            false, "search:" + strQuery, !bForce);
    }
    void OnlineSession::openBook(int nId, bool bForce)
    {
        m_pImpl->run(
            Kind::Book,
            [nId](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
            {
                Client oC(oO, pT);
                Result oR;
                oR.m_oNovel = oC.novel(nId, CachePolicy::NetworkOnly);
                oR.m_vVolumes = oC.index(nId, CachePolicy::NetworkOnly);
                oR.m_bSuccess = true;
                return oR;
            },
            false, QString("book:%1").arg(nId), !bForce);
    }
    void OnlineSession::openChapter(int nNovel, int nChapter, bool bForce)
    {
        m_pImpl->run(
            Kind::Chapter,
            [nNovel, nChapter](ClientOptions oO, std::shared_ptr<HttpTransport> pT)
            {
                Client oC(oO, pT);
                Result oR;
                oR.m_strText = QString::fromUtf8(
                    oC.chapter(nNovel, nChapter, CachePolicy::NetworkOnly).c_str());
                oR.m_nNovelId = nNovel;
                oR.m_nChapterId = nChapter;
                oR.m_vImageUrls = formatChapter({}, oR.m_strText).m_vImages;
                oR.m_bSuccess = true;
                return oR;
            },
            false, QString("chapter:%1:%2").arg(nNovel).arg(nChapter), !bForce);
    }
    void OnlineSession::download(int nId, bool bImages)
    {
        auto strDirectory = m_pImpl->m_strDirectory;
        auto pFlag = m_pImpl->m_pCancelled;
        m_pImpl->run(
            Kind::Download,
            [this, nId, bImages, strDirectory, pFlag](ClientOptions oO,
                                                      std::shared_ptr<HttpTransport> pT)
            {
                DownloadStoreOptions oStore;
                oStore.m_oDirectory = nativePath(strDirectory);
                oStore.m_vImageOrigins.push_back("https://pic.777743.xyz");
                auto oAssetRetry = clientAssetRetryOptions(false);
                oAssetRetry.m_oDelay = std::chrono::seconds(2);
                NovelDownloader oDownload(
                    oO, oStore, pT, makeRetryingTransport(makeClientAssetTransport(), oAssetRetry));
                DownloadRequest oRequest;
                oRequest.m_bIncludeImages = bImages;
                oRequest.m_bIncludeCover = bImages;
                oRequest.m_fnProgress =
                    [this, pFlag, oLast = std::chrono::steady_clock::time_point{}](
                        const DownloadStatus& oS) mutable
                {
                    auto oNow = std::chrono::steady_clock::now();
                    if (oNow - oLast > std::chrono::milliseconds(250))
                    {
                        oLast = oNow;
                        QString strMessage = QString("正在下载 · 章节 %1/%2 · 插图 %3/%4")
                                                 .arg(oS.m_nChaptersCompleted)
                                                 .arg(oS.m_nChaptersTotal)
                                                 .arg(oS.m_nImagesCompleted)
                                                 .arg(oS.m_nImagesTotal);
                        const auto nCompleted =
                                       qint64(oS.m_nChaptersCompleted + oS.m_nImagesCompleted),
                                   nTotal = qint64(oS.m_nChaptersTotal + oS.m_nImagesTotal);
                        QMetaObject::invokeMethod(
                            this,
                            [this, strMessage, nCompleted, nTotal]
                            {
                                emit statusChanged(strMessage);
                                emit progressChanged(nCompleted, nTotal);
                            },
                            Qt::QueuedConnection);
                    }
                    return !pFlag->load();
                };
                oDownload.download(nId, oRequest);
                Result oR;
                oR.m_bSuccess = true;
                oR.m_nId = nId;
                oR.m_strText = "缓存完成，可以在「下载缓存」中离线阅读。";
                return oR;
            });
    }
    void OnlineSession::downloadToLocal(int nId, bool bImages, const QString& strDestination)
    {
        const QString strCacheDirectory = m_pImpl->m_strDirectory;
        const auto pFlag = m_pImpl->m_pCancelled;
        m_pImpl->run(
            Kind::Export,
            [this, nId, bImages, strDestination, strCacheDirectory, pFlag](
                ClientOptions oOptions, std::shared_ptr<HttpTransport> pTransport)
            {
                if (!exportDirectoryAvailable(strDestination))
                {
                    throw Error(ErrorCode::Io, "保存位置不可用，请重新选择目录。");
                }
                QDir().mkpath(strCacheDirectory);
                QTemporaryDir oWork(QDir(strCacheDirectory).filePath(".local-download-XXXXXX"));
                if (!oWork.isValid())
                {
                    throw Error(ErrorCode::Io, "无法准备临时下载空间。");
                }
                auto pLastReport = std::make_shared<std::chrono::steady_clock::time_point>();
                auto pLastStage = std::make_shared<QString>();
                auto fnReport = [this, pFlag, pLastReport, pLastStage](
                    const QString& strMessage, qint64 nDone, qint64 nTotal)
                {
                    if (pFlag->load())
                    {
                        return false;
                    }
                    const auto oNow = std::chrono::steady_clock::now();
                    if (strMessage == *pLastStage && nDone != nTotal &&
                        oNow - *pLastReport < std::chrono::milliseconds(250))
                    {
                        return true;
                    }
                    *pLastReport = oNow;
                    *pLastStage = strMessage;
                    QMetaObject::invokeMethod(this, [this, strMessage, nDone, nTotal]
                    {
                        emit statusChanged(strMessage);
                        emit progressChanged(nDone, nTotal);
                    }, Qt::QueuedConnection);
                    return !pFlag->load();
                };
                ExportOptions oExport;
                oExport.m_oDirectory = nativePath(oWork.path() + "/portable");
                oExport.m_bIncludeImages = bImages;
                oExport.m_fnProgress = [fnReport](const ExportProgress& oProgress)
                {
                    return fnReport("正在整理小说文件…",
                                    qint64(oProgress.m_nChaptersCompleted + oProgress.m_nImagesCompleted),
                                    qint64(oProgress.m_nChaptersTotal + oProgress.m_nImagesTotal));
                };
                ExportResult oExported;
                bool bExported = false;
                auto pCached = openStore(strCacheDirectory, QString::fromStdString(oOptions.m_strEndpoint));
                try
                {
                    if (pCached->status(nId).m_bHasReadableVersion)
                    {
                        oExported = exportNovel(*pCached, nId, oExport);
                        bExported = true;
                    }
                }
                catch (const Error& oError)
                {
                    if (oError.code() != ErrorCode::CacheMiss)
                    {
                        throw;
                    }
                }
                if (!bExported)
                {
                    DownloadStoreOptions oStore;
                    oStore.m_oDirectory = nativePath(oWork.path() + "/source");
                    oStore.m_vImageOrigins.push_back("https://pic.777743.xyz");
                    NovelDownloader oDownload(oOptions, oStore, pTransport,
                        makeRetryingTransport(makeClientAssetTransport(), clientAssetRetryOptions(false)));
                    DownloadRequest oRequest;
                    oRequest.m_bIncludeImages = bImages;
                    oRequest.m_bIncludeCover = bImages;
                    oRequest.m_fnProgress = [fnReport, pFlag, oLast = std::chrono::steady_clock::time_point{}](
                        const DownloadStatus& oStatus) mutable
                    {
                        const auto oNow = std::chrono::steady_clock::now();
                        if (oNow - oLast < std::chrono::milliseconds(250))
                        {
                            return !pFlag->load();
                        }
                        oLast = oNow;
                        return fnReport(QString("正在下载到本地 · 章节 %1/%2 · 插图 %3/%4")
                            .arg(oStatus.m_nChaptersCompleted).arg(oStatus.m_nChaptersTotal)
                            .arg(oStatus.m_nImagesCompleted).arg(oStatus.m_nImagesTotal),
                            qint64(oStatus.m_nChaptersCompleted + oStatus.m_nImagesCompleted),
                            qint64(oStatus.m_nChaptersTotal + oStatus.m_nImagesTotal));
                    };
                    oDownload.download(nId, oRequest);
                    oExported = exportNovel(oDownload, nId, oExport);
                }
                fnReport("正在保存到所选目录…", 0, 0);
                const QString strLocation = publishExportDirectory(
                    QString::fromStdString(oExported.m_oDirectory.u8string()), strDestination,
                    [pFlag]
                    {
                        return !pFlag->load();
                    });
                Result oResult;
                oResult.m_bSuccess = true;
                oResult.m_nId = nId;
                oResult.m_strExportedLocation = strLocation;
                oResult.m_strText = "已保存到本地：" + strLocation;
                return oResult;
            });
    }
    void OnlineSession::addToShelf(int nId)
    {
        const auto strExpected = m_pImpl->m_strUser;
        m_pImpl->run(
            Kind::ShelfAdd,
            [nId, strExpected](ClientOptions oOptions, std::shared_ptr<HttpTransport> pTransport)
            {
                Client oClient(oOptions, pTransport);
                auto oUser = oClient.userInfo();
                if (QString::fromUtf8(oUser.m_strUsername.c_str()) != strExpected)
                    throw Error(ErrorCode::Cancelled, "Account changed before bookshelf operation");
                oClient.addToBookshelf(nId);
                Result oResult;
                oResult.m_bSuccess = true;
                oResult.m_nId = nId;
                oResult.m_strText = "已加入书架。";
                return oResult;
            });
    }

} // namespace Wenku8::Reader
