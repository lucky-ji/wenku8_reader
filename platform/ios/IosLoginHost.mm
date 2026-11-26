// SPDX-License-Identifier: GPL-2.0-only
#include "LoginHost.h"
#include "MobileBrowserProtocol.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QWidget>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#import <dispatch/dispatch.h>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

namespace
{
    enum class NavigationEvent
    {
        Started,
        Finished,
        Failed,
        Terminated
    };
}

@interface Wenku8IosBrowserDelegate : NSObject <WKNavigationDelegate>
{
@public
    std::function<QUrl(WKNavigationAction*)> m_fnDestination;
    std::function<bool(NSURL*)> m_fnAllowsResponse;
    std::function<void(WKWebView*, WKNavigation*, NavigationEvent)> m_fnNavigation;
    std::function<void()> m_fnCancel;
}
- (void)cancelLogin;
@end

@implementation Wenku8IosBrowserDelegate
- (void)webView:(WKWebView*)pView decidePolicyForNavigationAction:(WKNavigationAction*)pAction
 decisionHandler:(void (^)(WKNavigationActionPolicy))fnDecision
{
    if (pAction.targetFrame && !pAction.targetFrame.isMainFrame)
    {
        fnDecision(WKNavigationActionPolicyAllow);
        return;
    }

    const auto oDestination = m_fnDestination ? m_fnDestination(pAction) : QUrl();
    if (!oDestination.isValid() || oDestination.isEmpty())
    {
        fnDecision(WKNavigationActionPolicyCancel);
        return;
    }

    NSURL* pUrl = oDestination.toNSURL();
    if (pAction.targetFrame && QUrl::fromNSURL(pAction.request.URL) == oDestination)
    {
        fnDecision(WKNavigationActionPolicyAllow);
        return;
    }

    fnDecision(WKNavigationActionPolicyCancel);
    // Login forms are normalized in the page before submission. Never replay
    // a POST by replacing its destination or falling back to a GET.
    if ([pAction.request.HTTPMethod isEqualToString:@"GET"])
    {
        NSMutableURLRequest* pRequest = [pAction.request mutableCopy];
        pRequest.URL = pUrl;
        [pView loadRequest:pRequest];
    }
}

- (void)webView:(WKWebView*)pView decidePolicyForNavigationResponse:(WKNavigationResponse*)pResponse
 decisionHandler:(void (^)(WKNavigationResponsePolicy))fnDecision
{
    Q_UNUSED(pView);
    const bool bAllow = !pResponse.isForMainFrame ||
        (m_fnAllowsResponse && m_fnAllowsResponse(pResponse.response.URL));
    fnDecision(bAllow ? WKNavigationResponsePolicyAllow : WKNavigationResponsePolicyCancel);
}

- (void)webView:(WKWebView*)pView didStartProvisionalNavigation:(WKNavigation*)pNavigation
{
    if (m_fnNavigation)
    {
        m_fnNavigation(pView, pNavigation, NavigationEvent::Started);
    }
}

- (void)webView:(WKWebView*)pView didFinishNavigation:(WKNavigation*)pNavigation
{
    if (m_fnNavigation)
    {
        m_fnNavigation(pView, pNavigation, NavigationEvent::Finished);
    }
}

- (void)webView:(WKWebView*)pView didFailNavigation:(WKNavigation*)pNavigation withError:(NSError*)pError
{
    Q_UNUSED(pError);
    if (m_fnNavigation)
    {
        m_fnNavigation(pView, pNavigation, NavigationEvent::Failed);
    }
}

- (void)webView:(WKWebView*)pView didFailProvisionalNavigation:(WKNavigation*)pNavigation
      withError:(NSError*)pError
{
    [self webView:pView didFailNavigation:pNavigation withError:pError];
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)pView
{
    if (m_fnNavigation)
    {
        m_fnNavigation(pView, nil, NavigationEvent::Terminated);
    }
}

- (void)cancelLogin
{
    if (m_fnCancel)
    {
        m_fnCancel();
    }
}
@end

static void initIosLoginResources()
{
    Q_INIT_RESOURCE(mobile_browser);
}

namespace Wenku8::Reader
{
    namespace
    {
        using namespace std::chrono_literals;

        bool withinOrigin(const QUrl& oOrigin, const QUrl& oUrl)
        {
            return MobileBrowserProtocol::sameOrigin(oOrigin, oUrl) && oUrl.userInfo().isEmpty();
        }

        QUrl destination(const QUrl& oOrigin, const QUrl& oUrl)
        {
            if (withinOrigin(oOrigin, oUrl))
            {
                return oUrl;
            }

            const auto strHost = oUrl.host();
            const auto strOriginHost = oOrigin.host();
            const bool bAlias = (strHost == "wenku8.net" && strOriginHost == "www.wenku8.net") ||
                                (strHost == "www.wenku8.net" && strOriginHost == "wenku8.net");
            const bool bLegacy = oUrl.scheme() == "http" && oUrl.port(80) == 80 && oOrigin.port(443) == 443;
            const bool bHttps = oUrl.scheme() == "https" && oUrl.port(443) == oOrigin.port(443);
            if (!oUrl.isValid() || !oUrl.userInfo().isEmpty() || !(bLegacy || bHttps) ||
                !(bAlias || strHost == strOriginHost))
            {
                return {};
            }

            auto oCanonical = oUrl;
            oCanonical.setScheme(oOrigin.scheme());
            oCanonical.setHost(strOriginHost);
            oCanonical.setPort(oOrigin.port());
            return oCanonical;
        }

        struct Pending
        {
            const QByteArray m_baConfig;
            const std::size_t m_nMaxReply;
            std::mutex m_oMutex;
            std::condition_variable m_oReady;
            bool m_bDone = false;
            QByteArray m_baReply;

            Pending(QByteArray baConfig, std::size_t nLimit)
                : m_baConfig(std::move(baConfig)), m_nMaxReply(nLimit * 4 / 3 + 131072)
            {
            }

            void finish(QByteArray baReply)
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                if (!m_bDone)
                {
                    m_baReply = std::move(baReply);
                    m_bDone = true;
                    m_oReady.notify_all();
                }
            }

            bool done()
            {
                std::lock_guard<std::mutex> oLock(m_oMutex);
                return m_bDone;
            }
        };

        struct BrowserLane
        {
            const QUrl m_oOrigin;
            const QString m_strFetch;
            std::atomic_bool m_bClosed{false};
            std::timed_mutex m_oSerial;

            // WKWebView state and ownership of pending work are UI-thread only.
            __weak WKWebView* m_pView = nil;
            WKNavigation* m_pNavigation = nil;
            std::shared_ptr<Pending> m_pPending;
            bool m_bReady = false;
            bool m_bFetching = false;

            BrowserLane(const QUrl& oOrigin, const QString& strFetch)
                : m_oOrigin(oOrigin), m_strFetch(strFetch)
            {
            }
        };

        WKContentWorld* requestWorld()
        {
            return [WKContentWorld worldWithName:@"Wenku8ReaderSession"];
        }

        void finishPending(BrowserLane& oLane, QByteArray baReply)
        {
            auto pPending = std::move(oLane.m_pPending);
            oLane.m_bFetching = false;
            if (pPending)
            {
                pPending->finish(std::move(baReply));
            }
        }

        void abortPending(BrowserLane& oLane)
        {
            if (oLane.m_bFetching)
            {
                [oLane.m_pView evaluateJavaScript:@"window.__wenku8SdkJob?.controller.abort()"
                                          inFrame:nil inContentWorld:requestWorld() completionHandler:nil];
            }
            finishPending(oLane, R"({"done":true,"error":"cancel"})");
        }

        void loadLogin(BrowserLane& oLane)
        {
            WKWebView* pView = oLane.m_pView;
            oLane.m_bReady = false;
            const auto oUrl = oLane.m_oOrigin.resolved(QUrl("/login.php"));
            oLane.m_pNavigation = [pView loadRequest:[NSURLRequest requestWithURL:oUrl.toNSURL()]];
            if (!oLane.m_pNavigation)
            {
                finishPending(oLane, {});
            }
        }

        // The escaping WebKit completion block must own its copy of the lane.
        void startPending(const std::shared_ptr<BrowserLane> pLane)
        {
            const auto pPending = pLane->m_pPending;
            if (!pPending || pPending->done() || pLane->m_bFetching || !pLane->m_bReady)
            {
                return;
            }

            WKWebView* pView = pLane->m_pView;
            if (pLane->m_bClosed || !pView || !withinOrigin(pLane->m_oOrigin, QUrl::fromNSURL(pView.URL)))
            {
                finishPending(*pLane, {});
                return;
            }

            NSData* pData = [NSData dataWithBytes:pPending->m_baConfig.constData()
                                         length:NSUInteger(pPending->m_baConfig.size())];
            id pConfig = [NSJSONSerialization JSONObjectWithData:pData options:0 error:nullptr];
            if (![pConfig isKindOfClass:NSDictionary.class])
            {
                finishPending(*pLane, {});
                return;
            }

            pLane->m_bFetching = true;
            [pView callAsyncJavaScript:pLane->m_strFetch.toNSString()
                            arguments:@{ @"config": pConfig }
                              inFrame:nil
                       inContentWorld:requestWorld()
                    completionHandler:^(id pResult, NSError* pError)
            {
                if (pLane->m_pPending != pPending)
                {
                    return;
                }
                if (pError || ![pResult isKindOfClass:NSString.class])
                {
                    finishPending(*pLane, {});
                    return;
                }

                NSString* pJson = pResult;
                if ([pJson lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > pPending->m_nMaxReply)
                {
                    finishPending(*pLane, R"({"done":true,"error":"size"})");
                    return;
                }
                finishPending(*pLane, QString::fromNSString(pJson).toUtf8());
            }];
        }

        class IosTransport final : public HttpTransport
        {
            std::shared_ptr<BrowserLane> m_pLane;

          public:
            explicit IosTransport(std::shared_ptr<BrowserLane> pLane) : m_pLane(std::move(pLane))
            {
            }

            bool supportsSameOriginGetRedirects() const noexcept override
            {
                return true;
            }

            void clearCookies() override
            {
                throw Error(ErrorCode::Unsupported, "Use Switch Account to reset the browser session");
            }

            HttpResponse perform(const HttpRequest& oRequest, const TransferOptions& oLimits) override
            {
                if (!qApp || QThread::currentThread() == qApp->thread())
                {
                    throw Error(ErrorCode::InvalidArgument, "Call iOS browser transport on a worker");
                }

                const auto pLane = m_pLane;
                const auto oConfig = MobileBrowserProtocol::requestConfig(pLane->m_oOrigin, oRequest, oLimits);
                const auto oDeadline = std::chrono::steady_clock::now() + oLimits.m_oTimeout;
                const auto fnCheck = [&]
                {
                    if (pLane->m_bClosed || (oLimits.m_fnProgress && !oLimits.m_fnProgress(0, 0)))
                    {
                        throw Error(ErrorCode::Cancelled, "iOS browser request cancelled");
                    }
                    if (std::chrono::steady_clock::now() >= oDeadline)
                    {
                        throw Error(ErrorCode::Network, "iOS browser request timed out");
                    }
                };

                std::unique_lock<std::timed_mutex> oSerial(pLane->m_oSerial, std::defer_lock);
                while (!oSerial.try_lock_for(25ms))
                {
                    fnCheck();
                }
                fnCheck();

                const auto pPending = std::make_shared<Pending>(QJsonDocument(oConfig).toJson(QJsonDocument::Compact),
                                                                 oLimits.m_nMaxResponseBytes);
                dispatch_async(dispatch_get_main_queue(), ^
                {
                    if (pLane->m_bClosed || pPending->done())
                    {
                        pPending->finish(R"({"done":true,"error":"cancel"})");
                        return;
                    }
                    pLane->m_pPending = pPending;
                    if (pLane->m_bReady)
                    {
                        startPending(pLane);
                    }
                    else if (!pLane->m_pNavigation)
                    {
                        loadLogin(*pLane);
                    }
                });

                QByteArray baReply;
                try
                {
                    std::unique_lock<std::mutex> oLock(pPending->m_oMutex);
                    while (!pPending->m_bDone)
                    {
                        pPending->m_oReady.wait_for(oLock, 25ms);
                        oLock.unlock();
                        fnCheck();
                        oLock.lock();
                    }
                    baReply = std::move(pPending->m_baReply);
                }
                catch (...)
                {
                    pPending->finish({});
                    dispatch_async(dispatch_get_main_queue(), ^
                    {
                        if (pLane->m_pPending == pPending)
                        {
                            abortPending(*pLane);
                        }
                    });
                    throw;
                }

                auto oResponse = MobileBrowserProtocol::response(pLane->m_oOrigin, baReply,
                                                                 oLimits.m_nMaxResponseBytes);
                const auto nBody = oResponse.m_strBody.size();
                if (oLimits.m_fnProgress && !oLimits.m_fnProgress(nBody, nBody))
                {
                    throw Error(ErrorCode::Cancelled, "iOS browser request cancelled");
                }
                return oResponse;
            }
        };

        QString readScript(const QString& strPath)
        {
            QFile oFile(":/mobile-browser/" + strPath);
            if (!oFile.open(QIODevice::ReadOnly))
            {
                throw Error(ErrorCode::Io, "iOS browser script unavailable");
            }
            return QString::fromUtf8(oFile.readAll()).trimmed();
        }

        UIColor* nativeColor(const QColor& oColor)
        {
            return [UIColor colorWithRed:oColor.redF() green:oColor.greenF()
                                    blue:oColor.blueF() alpha:oColor.alphaF()];
        }

        class IosLoginHost final : public LoginHost
        {
            QPointer<QWidget> m_pOwner;
            std::shared_ptr<BrowserLane> m_pMain;
            std::shared_ptr<BrowserLane> m_pDownload;
            WKWebsiteDataStore* m_pStore = WKWebsiteDataStore.defaultDataStore;
            Wenku8IosBrowserDelegate* m_pDelegate = [[Wenku8IosBrowserDelegate alloc] init];
            WKUserScript* m_pNavigationScript = nil;
            WKWebView* m_pLoginView = nil;
            WKWebView* m_pDownloadView = nil;
            UIView* m_pOverlay = [[UIView alloc] initWithFrame:CGRectZero];
            UIView* m_pHeader = [[UIView alloc] initWithFrame:CGRectZero];
            UIStackView* m_pStack = nil;
            UILabel* m_pTitle = [[UILabel alloc] initWithFrame:CGRectZero];
            UILabel* m_pHint = [[UILabel alloc] initWithFrame:CGRectZero];
            UIButton* m_pBack = [UIButton buttonWithType:UIButtonTypeSystem];
            UIActivityIndicatorView* m_pLoading = [[UIActivityIndicatorView alloc]
                initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
            NSArray<NSLayoutConstraint*>* m_pAttached = nil;
            NSArray<NSLayoutConstraint*>* m_pExpanded = nil;
            NSArray<NSLayoutConstraint*>* m_pCollapsed = nil;
            QString m_strHint;
            std::string m_strAgent;
            bool m_bVisible = false;
            bool m_bResetting = false;
            bool m_bLoadFailed = false;
            quint64 m_nReset = 0;

            WKWebView* createView(const std::shared_ptr<BrowserLane>& pLane)
            {
                WKWebViewConfiguration* pConfig = [[WKWebViewConfiguration alloc] init];
                pConfig.websiteDataStore = m_pStore;
                [pConfig.userContentController addUserScript:m_pNavigationScript];
                WKWebView* pView = [[WKWebView alloc] initWithFrame:CGRectMake(0, 0, 1, 1)
                                                      configuration:pConfig];
                pView.navigationDelegate = m_pDelegate;
                pLane->m_pView = pView;
                return pView;
            }

            void stopLane(BrowserLane& oLane)
            {
                const bool bWasLoading = oLane.m_pNavigation != nil;
                oLane.m_bReady = false;
                oLane.m_pNavigation = nil;
                abortPending(oLane);
                if (bWasLoading)
                {
                    [oLane.m_pView stopLoading];
                }
            }

            void resetDownload()
            {
                if (m_pDownload)
                {
                    m_pDownload->m_bClosed = true;
                    m_pDownloadView.navigationDelegate = nil;
                    stopLane(*m_pDownload);
                    [m_pDownloadView removeFromSuperview];
                }
                m_pDownload = std::make_shared<BrowserLane>(m_pMain->m_oOrigin, m_pMain->m_strFetch);
                m_pDownloadView = createView(m_pDownload);
                m_pDownloadView.alpha = 0.01;
                m_pDownloadView.userInteractionEnabled = NO;
                m_pDownloadView.accessibilityElementsHidden = YES;
                [m_pOverlay.superview insertSubview:m_pDownloadView belowSubview:m_pOverlay];
            }

            void buildOverlay()
            {
                m_pOverlay.translatesAutoresizingMaskIntoConstraints = NO;
                m_pOverlay.clipsToBounds = YES;
                m_pTitle.text = @"登录文库";
                m_pTitle.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
                m_pTitle.adjustsFontForContentSizeCategory = YES;
                m_pTitle.translatesAutoresizingMaskIntoConstraints = NO;
                m_pBack.translatesAutoresizingMaskIntoConstraints = NO;
                [m_pBack setTitle:@"返回" forState:UIControlStateNormal];
                [m_pBack addTarget:m_pDelegate action:@selector(cancelLogin) forControlEvents:UIControlEventTouchUpInside];
                m_pLoading.translatesAutoresizingMaskIntoConstraints = NO;
                m_pLoading.hidesWhenStopped = YES;
                [m_pHeader addSubview:m_pBack];
                [m_pHeader addSubview:m_pTitle];
                [m_pHeader addSubview:m_pLoading];
                NSLayoutConstraint* pHeaderHeight = [m_pHeader.heightAnchor constraintEqualToConstant:52];
                pHeaderHeight.priority = UILayoutPriorityRequired - 1;
                [NSLayoutConstraint activateConstraints:@[
                    pHeaderHeight,
                    [m_pBack.leadingAnchor constraintEqualToAnchor:m_pHeader.leadingAnchor constant:8],
                    [m_pBack.centerYAnchor constraintEqualToAnchor:m_pHeader.centerYAnchor],
                    [m_pBack.widthAnchor constraintGreaterThanOrEqualToConstant:56],
                    [m_pBack.heightAnchor constraintGreaterThanOrEqualToConstant:44],
                    [m_pTitle.centerXAnchor constraintEqualToAnchor:m_pHeader.centerXAnchor],
                    [m_pTitle.centerYAnchor constraintEqualToAnchor:m_pHeader.centerYAnchor],
                    [m_pLoading.trailingAnchor constraintEqualToAnchor:m_pHeader.trailingAnchor constant:-18],
                    [m_pLoading.centerYAnchor constraintEqualToAnchor:m_pHeader.centerYAnchor]
                ]];

                m_pHint.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
                m_pHint.adjustsFontForContentSizeCategory = YES;
                m_pHint.textAlignment = NSTextAlignmentCenter;
                m_pHint.numberOfLines = 0;
                m_pStack = [[UIStackView alloc] initWithArrangedSubviews:@[m_pHeader, m_pLoginView, m_pHint]];
                m_pStack.axis = UILayoutConstraintAxisVertical;
                m_pStack.translatesAutoresizingMaskIntoConstraints = NO;
                [m_pOverlay addSubview:m_pStack];

                auto* pSafeArea = m_pOverlay.safeAreaLayoutGuide;
                [NSLayoutConstraint activateConstraints:@[
                    [m_pStack.leadingAnchor constraintEqualToAnchor:pSafeArea.leadingAnchor],
                    [m_pStack.trailingAnchor constraintEqualToAnchor:pSafeArea.trailingAnchor],
                    [m_pStack.topAnchor constraintEqualToAnchor:pSafeArea.topAnchor],
                    [m_pStack.bottomAnchor constraintEqualToAnchor:pSafeArea.bottomAnchor]
                ]];
                updateOverlay();
            }

            bool attachOverlay()
            {
                if (!m_pOwner)
                {
                    return false;
                }
                UIView* pParent = (__bridge UIView*)reinterpret_cast<void*>(m_pOwner->window()->winId());
                if (!pParent)
                {
                    return false;
                }
                if (m_pOverlay.superview == pParent)
                {
                    return true;
                }
                if (m_pAttached)
                {
                    [NSLayoutConstraint deactivateConstraints:m_pAttached];
                    [NSLayoutConstraint deactivateConstraints:m_pExpanded];
                    [NSLayoutConstraint deactivateConstraints:m_pCollapsed];
                }
                [pParent addSubview:m_pDownloadView];
                [pParent addSubview:m_pOverlay];
                m_pAttached = @[
                    [m_pOverlay.leadingAnchor constraintEqualToAnchor:pParent.leadingAnchor],
                    [m_pOverlay.topAnchor constraintEqualToAnchor:pParent.topAnchor]
                ];
                m_pExpanded = @[
                    [m_pOverlay.trailingAnchor constraintEqualToAnchor:pParent.trailingAnchor],
                    [m_pOverlay.bottomAnchor constraintEqualToAnchor:pParent.bottomAnchor]
                ];
                m_pCollapsed = @[
                    [m_pOverlay.widthAnchor constraintEqualToConstant:1],
                    [m_pOverlay.heightAnchor constraintEqualToConstant:1]
                ];
                [NSLayoutConstraint activateConstraints:m_pAttached];
                updateOverlay();
                return true;
            }

            void updateHint()
            {
                const auto strHint = m_bLoadFailed
                    ? QStringLiteral("登录页无法加载，请检查当前网络或轻小说文库的联网权限后重试。")
                    : m_strHint;
                m_pHint.text = strHint.toNSString();
                m_pHint.hidden = !m_bVisible || strHint.isEmpty();
            }

            void updateOverlay()
            {
                m_pHeader.hidden = !m_bVisible;
                m_pOverlay.alpha = m_bVisible ? 1 : 0.01;
                m_pOverlay.userInteractionEnabled = m_bVisible;
                m_pOverlay.accessibilityElementsHidden = !m_bVisible;
                updateHint();
                if (m_pAttached)
                {
                    [NSLayoutConstraint deactivateConstraints:m_bVisible ? m_pCollapsed : m_pExpanded];
                    [NSLayoutConstraint activateConstraints:m_bVisible ? m_pExpanded : m_pCollapsed];
                }
                if (m_bVisible)
                {
                    [m_pOverlay.superview bringSubviewToFront:m_pOverlay];
                }
            }

            void navigationFailed(const std::shared_ptr<BrowserLane>& pLane)
            {
                pLane->m_bReady = false;
                pLane->m_pNavigation = nil;
                finishPending(*pLane, {});
                if (pLane == m_pMain)
                {
                    [m_pLoading stopAnimating];
                    m_bLoadFailed = true;
                    updateHint();
                }
            }

            void navigationEvent(WKWebView* pView, WKNavigation* pNavigation, NavigationEvent eEvent)
            {
                const auto pLane = pView == m_pLoginView ? m_pMain
                    : pView == m_pDownloadView ? m_pDownload : nullptr;
                if (!pLane || pLane->m_bClosed || m_bResetting)
                {
                    return;
                }

                if (eEvent == NavigationEvent::Started)
                {
                    pLane->m_pNavigation = pNavigation;
                    pLane->m_bReady = false;
                    if (pLane->m_bFetching)
                    {
                        finishPending(*pLane, {});
                    }
                    if (pLane == m_pMain)
                    {
                        m_bLoadFailed = false;
                        updateHint();
                        [m_pLoading startAnimating];
                    }
                    return;
                }
                if (eEvent == NavigationEvent::Terminated)
                {
                    navigationFailed(pLane);
                    return;
                }
                if (pLane->m_pNavigation != pNavigation)
                {
                    return;
                }
                if (eEvent == NavigationEvent::Failed || !withinOrigin(pLane->m_oOrigin, QUrl::fromNSURL(pView.URL)))
                {
                    navigationFailed(pLane);
                    return;
                }

                QPointer<IosLoginHost> pSelf(this);
                [pView evaluateJavaScript:@"window.__wenku8NavigationGuard ? navigator.userAgent : ''"
                         completionHandler:^(id pResult, NSError* pError)
                {
                    if (!pSelf || pLane->m_bClosed || pLane->m_pNavigation != pNavigation || pSelf->m_bResetting)
                    {
                        return;
                    }
                    if (pError || ![pResult isKindOfClass:NSString.class] || ![pResult length])
                    {
                        pSelf->navigationFailed(pLane);
                        return;
                    }
                    pLane->m_pNavigation = nil;
                    pLane->m_bReady = true;
                    startPending(pLane);
                    if (pLane == pSelf->m_pMain)
                    {
                        pSelf->m_strAgent = QString::fromNSString(pResult).toStdString();
                        [pSelf->m_pLoading stopAnimating];
                        emit pSelf->pageLoaded();
                    }
                }];
            }

            void resetStore()
            {
                m_bResetting = true;
                const auto pOldLane = m_pMain;
                pOldLane->m_bClosed = true;
                [m_pLoginView endEditing:YES];
                m_pLoginView.navigationDelegate = nil;
                stopLane(*pOldLane);
                [m_pStack removeArrangedSubview:m_pLoginView];
                [m_pLoginView removeFromSuperview];
                m_pMain = std::make_shared<BrowserLane>(pOldLane->m_oOrigin, pOldLane->m_strFetch);
                m_pLoginView = createView(m_pMain);
                m_pLoginView.userInteractionEnabled = NO;
                [m_pStack insertArrangedSubview:m_pLoginView atIndex:1];
                resetDownload();
                m_bLoadFailed = false;
                updateHint();
                [m_pLoading startAnimating];
                const auto nReset = ++m_nReset;
                QPointer<IosLoginHost> pSelf(this);
                [m_pStore removeDataOfTypes:WKWebsiteDataStore.allWebsiteDataTypes
                             modifiedSince:NSDate.distantPast
                         completionHandler:^
                {
                    if (!pSelf || pSelf->m_pMain->m_bClosed || nReset != pSelf->m_nReset)
                    {
                        return;
                    }
                    pSelf->m_bResetting = false;
                    pSelf->m_pLoginView.userInteractionEnabled = YES;
                    if (pSelf->m_bVisible)
                    {
                        pSelf->loadMainPage();
                    }
                }];
            }

            void loadMainPage()
            {
                stopLane(*m_pMain);
                m_bLoadFailed = false;
                updateHint();
                [m_pLoading startAnimating];
                loadLogin(*m_pMain);
                if (!m_pMain->m_pNavigation)
                {
                    navigationFailed(m_pMain);
                }
            }

          public:
            IosLoginHost(QWidget* pOwner, const QUrl& oOrigin) : m_pOwner(pOwner)
            {
                MobileBrowserProtocol::validateOrigin(oOrigin);
                initIosLoginResources();

                auto strFetch = readScript("mobile_fetch.js");
                strFetch.replace("CONFIG", "config");
                // Reuse the Android fetch contract in a separate WK content world.
                strFetch.prepend("const bStarted = ");
                strFetch += "\nif (!bStarted) return null;\n"
                            "const s = window.__wenku8SdkJob;\n"
                            "await s.result;\n"
                            "return JSON.stringify({done:s.done,error:s.error,status:s.status,"
                            "url:s.url,headers:s.headers,body:s.body});";
                m_pMain = std::make_shared<BrowserLane>(oOrigin, strFetch);

                auto oScriptOrigin = oOrigin;
                if (oScriptOrigin.port() == 443)
                {
                    oScriptOrigin.setPort(-1);
                }
                const auto baOrigin = QJsonDocument(QJsonArray{
                    oScriptOrigin.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)})
                    .toJson(QJsonDocument::Compact);
                const auto strOrigin = QString::fromUtf8(baOrigin.mid(1, baOrigin.size() - 2));
                auto strNavigation = readScript("mobile_navigation.js") + "\n;\n" +
                    readScript("remember_login.js") + "\n;\n" + readScript("login_ui.js");
                strNavigation.replace("ORIGIN", strOrigin);
                m_pNavigationScript = [[WKUserScript alloc] initWithSource:strNavigation.toNSString()
                    injectionTime:WKUserScriptInjectionTimeAtDocumentEnd forMainFrameOnly:YES];

                m_pDelegate->m_fnDestination = [this, oOrigin](WKNavigationAction* pAction)
                {
                    return m_bResetting ? QUrl() : destination(oOrigin, QUrl::fromNSURL(pAction.request.URL));
                };
                m_pDelegate->m_fnAllowsResponse = [this, oOrigin](NSURL* pUrl)
                {
                    return !m_bResetting && withinOrigin(oOrigin, QUrl::fromNSURL(pUrl));
                };
                m_pDelegate->m_fnNavigation = [this](WKWebView* pView, WKNavigation* pNavigation, NavigationEvent eEvent)
                {
                    navigationEvent(pView, pNavigation, eEvent);
                };
                m_pDelegate->m_fnCancel = [this]
                {
                    accept();
                    emit cancelled();
                };

                m_pLoginView = createView(m_pMain);
                resetDownload();
                buildOverlay();
            }

            ~IosLoginHost() override
            {
                close();
            }

            bool visible() const override
            {
                return m_bVisible;
            }

            bool hasPage() const override
            {
                return m_pMain->m_bReady;
            }

            bool persistent() const override
            {
                return m_pStore.persistent;
            }

            void show(bool bReset) override
            {
                if (m_pMain->m_bClosed)
                {
                    return;
                }
                if (!attachOverlay())
                {
                    emit unavailable();
                    return;
                }
                const bool bOpening = !m_bVisible;
                m_bVisible = true;
                updateOverlay();
                if (bReset && !m_bResetting)
                {
                    resetStore();
                }
                else if (bOpening || !hasPage())
                {
                    openLogin();
                }
            }

            void openLogin() override
            {
                if (m_pMain->m_bClosed || m_bResetting)
                {
                    return;
                }
                if (!attachOverlay())
                {
                    m_bVisible = false;
                    emit unavailable();
                    return;
                }
                resetDownload();
                loadMainPage();
            }

            void accept() override
            {
                [m_pOverlay endEditing:YES];
                [m_pLoading stopAnimating];
                m_bVisible = false;
                updateOverlay();
            }

            void close() override
            {
                if (m_pMain->m_bClosed.exchange(true))
                {
                    return;
                }
                ++m_nReset;
                m_pDownload->m_bClosed = true;
                m_pLoginView.navigationDelegate = nil;
                m_pDownloadView.navigationDelegate = nil;
                stopLane(*m_pMain);
                stopLane(*m_pDownload);
                accept();
                [m_pDownloadView removeFromSuperview];
                [m_pOverlay removeFromSuperview];
                m_pDelegate->m_fnDestination = {};
                m_pDelegate->m_fnAllowsResponse = {};
                m_pDelegate->m_fnNavigation = {};
                m_pDelegate->m_fnCancel = {};
            }

            void setHint(const QString& strHint) override
            {
                m_strHint = strHint;
                updateHint();
            }

            void setTheme(Theme eTheme) override
            {
                const auto oColors = colors(eTheme);
                m_pOverlay.overrideUserInterfaceStyle = eTheme == Theme::Dark ? UIUserInterfaceStyleDark
                                                                               : UIUserInterfaceStyleLight;
                m_pOverlay.backgroundColor = nativeColor(oColors.m_oPanel);
                m_pTitle.textColor = nativeColor(oColors.m_oText);
                m_pHint.textColor = nativeColor(oColors.m_oMuted);
                m_pBack.tintColor = nativeColor(oColors.m_oAccent);
                m_pLoading.color = nativeColor(oColors.m_oAccent);
            }

            std::string userAgent() const override
            {
                return m_strAgent;
            }

            std::shared_ptr<HttpTransport> transport() const override
            {
                return std::make_shared<IosTransport>(m_pMain);
            }

            std::shared_ptr<HttpTransport> downloadTransport() const override
            {
                return std::make_shared<IosTransport>(m_pDownload);
            }
        };
    }

    std::unique_ptr<LoginHost> makeLoginHost(QWidget* pOwner, const QUrl& oOrigin, const QString&)
    {
        return std::make_unique<IosLoginHost>(pOwner, oOrigin);
    }
} // namespace Wenku8::Reader
