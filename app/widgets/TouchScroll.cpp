// SPDX-License-Identifier: GPL-2.0-only
#include "TouchScroll.h"
#include <QAbstractScrollArea>
#include <QScroller>
#include <QScrollerProperties>
#include <QTouchEvent>
#include <QAbstractButton>
#include <QLineEdit>
#include <QGuiApplication>
#include <QInputMethod>
#include <QScrollBar>
namespace Wenku8::Reader
{
    TouchScroll::TouchScroll(QAbstractScrollArea* pArea, std::function<void(QPoint)> fnTap)
        : QObject(pArea), m_pArea(pArea), m_fnTap(std::move(fnTap))
    {
        m_pArea->viewport()->installEventFilter(this);
        setObjectName("touchScroll");
        connect(m_pArea->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
                [this](int nMinimum, int nMaximum)
                {
                    if (m_bEnabled && (nMinimum || nMaximum))
                        m_pArea->horizontalScrollBar()->setRange(0, 0);
                });
    }
    void TouchScroll::setEnabled(bool bEnabled)
    {
        if (m_bEnabled == bEnabled)
            return;
        resetPull();
        m_bEnabled = bEnabled;
        m_bActive = false;
        releaseFeedback();
        QScroller::ungrabGesture(m_pArea->viewport());
        auto pScroller = QScroller::scroller(m_pArea->viewport());
        pScroller->stop();
        m_pArea->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, bEnabled);
        if (m_bEnabled)
            m_pArea->horizontalScrollBar()->setRange(0, 0);
        auto oProperties = pScroller->scrollerProperties();
        oProperties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                                    QScrollerProperties::OvershootAlwaysOff);
        oProperties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                                    QScrollerProperties::OvershootAlwaysOff);
        oProperties.setScrollMetric(QScrollerProperties::FrameRate, QScrollerProperties::Fps60);
        // The default 5 mm threshold leaves short chapter-list drags behind the finger.
        oProperties.setScrollMetric(QScrollerProperties::DragStartDistance, 0.001);
        oProperties.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.5);
        oProperties.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.0);
        oProperties.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.14);
        pScroller->setScrollerProperties(oProperties);
    }
    void TouchScroll::setPageGestures(bool bEnabled, std::function<void(int)> fnTurn)
    {
        resetPull();
        m_bPaged = bEnabled;
        m_fnTurn = std::move(fnTurn);
        m_bActive = false;
        releaseFeedback();
        QScroller::scroller(m_pArea->viewport())->stop();
    }
    void TouchScroll::moveScroll(const QPointF& oPoint, qint64 nTimestamp)
    {
        const qreal fDelta = oPoint.y() - m_oScrollPoint.y();
        const int nDirection = fDelta > 0 ? 1 : fDelta < 0 ? -1 : 0;
        // Qt 5 replaces flick velocity immediately on a direction change. Ignore
        // small endpoint tremor, but accept an intentional reversal once it passes
        // eight logical pixels measured from the last accepted position.
        if (nDirection && m_nScrollDirection && nDirection != m_nScrollDirection &&
            qAbs(fDelta) < 8)
        {
            return;
        }
        m_oScrollPoint = QPointF(m_oStart.x(), oPoint.y());
        if (nDirection)
        {
            m_nScrollDirection = nDirection;
        }
        auto* pScroller = QScroller::scroller(m_pArea->viewport());
        pScroller->handleInput(QScroller::InputMove, m_oScrollPoint, nTimestamp);
        if (pScroller->state() == QScroller::Dragging)
        {
            m_bMoved = true;
            releaseFeedback();
        }
    }
    void TouchScroll::releaseFeedback()
    {
        if (m_pPressed)
            m_pPressed->setDown(false);
        m_pPressed = nullptr;
    }
    void TouchScroll::setPullToRefresh(std::function<bool()> fnAllowed,
                                       std::function<void(qreal)> fnProgress,
                                       std::function<void()> fnRefresh)
    {
        resetPull();
        m_fnPullAllowed = std::move(fnAllowed);
        m_fnPullProgress = std::move(fnProgress);
        m_fnPullRefresh = std::move(fnRefresh);
    }
    bool TouchScroll::canPull() const
    {
        return !m_bPaged && !m_bInterrupted && m_fnPullAllowed && m_fnPullRefresh &&
               m_fnPullAllowed();
    }
    void TouchScroll::resetPull()
    {
        const bool bShown = m_bPulling || m_fPullAmount > 0;
        m_bPulling = false;
        m_bPullBlocked = false;
        m_bPullAtTop = false;
        m_fPullAmount = 0;
        if (bShown && m_fnPullProgress)
            m_fnPullProgress(0);
    }
    bool TouchScroll::updatePull(const QPointF& oPoint, qint64 nTimestamp)
    {
        const auto oDelta = oPoint - m_oStart;
        const bool bHorizontal = qAbs(oDelta.x()) > 12 && qAbs(oDelta.x()) > qAbs(oDelta.y()) * 1.2;
        if (bHorizontal)
            m_bPullBlocked = true;
        if (m_bPulling)
        {
            if (!canPull() || m_bPullBlocked)
            {
                resetPull();
                m_bPullBlocked = true;
                QScroller::scroller(m_pArea->viewport())->stop();
                m_bMoved = true;
                return true;
            }
            const auto fDistance = oPoint.y() - m_fPullAnchorY;
            if (fDistance < -12)
            {
                // Withdrawing beyond the edge returns control to ordinary scrolling.
                resetPull();
                m_bPullBlocked = true;
                m_oScrollPoint = QPointF(m_oStart.x(), oPoint.y());
                m_nScrollDirection = 0;
                QScroller::scroller(m_pArea->viewport())
                    ->handleInput(QScroller::InputPress, m_oScrollPoint, nTimestamp);
                return true;
            }
            m_fPullAmount = qBound(qreal(0), fDistance / qreal(96), qreal(1.5));
            if (m_fnPullProgress)
                m_fnPullProgress(m_fPullAmount);
            return true;
        }
        if (m_bPullBlocked || !canPull())
            return false;
        const bool bTop =
            m_pArea->verticalScrollBar()->value() <= m_pArea->verticalScrollBar()->minimum();
        if (!bTop)
        {
            m_bPullAtTop = false;
            return false;
        }
        if (!m_bPullAtTop)
        {
            // Only count drag beyond the edge, not the distance used to scroll an
            // existing list back to the top. It may become eligible mid-gesture.
            m_bPullAtTop = true;
            m_fPullAnchorY = oPoint.y();
            return false;
        }
        const auto fDistance = oPoint.y() - m_fPullAnchorY;
        if (fDistance <= 12 || oDelta.y() <= 0 || qAbs(oDelta.y()) < qAbs(oDelta.x()) * 1.2)
            return false;
        m_bPulling = true;
        m_bMoved = true;
        releaseFeedback();
        QScroller::scroller(m_pArea->viewport())->stop();
        m_fPullAmount = qBound(qreal(0), fDistance / qreal(96), qreal(1.5));
        if (m_fnPullProgress)
            m_fnPullProgress(m_fPullAmount);
        return true;
    }
    qint64 TouchScroll::inputTimestamp(const QTouchEvent* pEvent)
    {
        // Preserve the platform's sampling intervals when queued events are
        // dispatched together after a costly layout/paint. Delivery wall time would
        // turn those moves into zero-millisecond samples and erase flick velocity.
        const auto nNative = quint64(pEvent->timestamp());
        const qint64 nElapsed = m_nTouchStartTimestamp && nNative >= m_nTouchStartTimestamp
                                    ? qint64(nNative - m_nTouchStartTimestamp)
                                    : m_oTime.elapsed();
        m_nLastInputTimestamp = qMax(m_nLastInputTimestamp, nElapsed);
        return m_nLastInputTimestamp;
    }
    bool TouchScroll::eventFilter(QObject* pObject, QEvent* pEvent)
    {
        if (!m_bEnabled)
            return QObject::eventFilter(pObject, pEvent);
        if (pEvent->type() == QEvent::Hide || pEvent->type() == QEvent::WindowDeactivate)
        {
            resetPull();
            m_bActive = false;
            releaseFeedback();
            QScroller::scroller(m_pArea->viewport())->stop();
            return false;
        }
        if (pEvent->type() != QEvent::TouchBegin && pEvent->type() != QEvent::TouchUpdate &&
            pEvent->type() != QEvent::TouchEnd && pEvent->type() != QEvent::TouchCancel)
            return QObject::eventFilter(pObject, pEvent);
        auto pScroller = QScroller::scroller(m_pArea->viewport());
        auto pTouch = static_cast<QTouchEvent*>(pEvent);
        pEvent->accept();
        if (pEvent->type() == QEvent::TouchCancel)
        {
            resetPull();
            m_bActive = false;
            releaseFeedback();
            pScroller->stop();
            return true;
        }
        if (pEvent->type() == QEvent::TouchBegin)
        {
            // Capture interruption before the press action can stop an animation
            // or scroller. The first press on moving content must never become a tap.
            resetPull();
            m_bInterrupted = pScroller->state() == QScroller::Scrolling ||
                             pScroller->state() == QScroller::Dragging;
            if (m_fnPress)
                m_fnPress();
            pScroller->stop();
            releaseFeedback();
            m_bActive = pTouch->touchPoints().size() == 1;
            m_bMoved = false;
            m_oTime.start();
            m_nTouchStartTimestamp = quint64(pTouch->timestamp());
            m_nLastInputTimestamp = 0;
            if (m_bActive)
            {
                auto oPoint = pTouch->touchPoints().front();
                m_nId = oPoint.id();
                m_oStart = oPoint.pos();
                m_oScrollPoint = m_oStart;
                m_nScrollDirection = 0;
                m_fPullAnchorY = m_oStart.y();
                m_bPullAtTop = canPull() && m_pArea->verticalScrollBar()->value() <=
                                                m_pArea->verticalScrollBar()->minimum();
                if (!m_bPaged)
                    pScroller->handleInput(QScroller::InputPress, m_oStart, 0);
                if (!m_fnTap && !m_bInterrupted)
                    for (auto pWidget = m_pArea->viewport()->childAt(m_oStart.toPoint());
                         pWidget && pWidget != m_pArea->viewport();
                         pWidget = pWidget->parentWidget())
                        if (auto pButton = qobject_cast<QAbstractButton*>(pWidget))
                        {
                            if (pButton->isEnabled())
                            {
                                m_pPressed = pButton;
                                m_pPressed->setDown(true);
                            }
                            break;
                        }
            }
            return true;
        }
        if (!m_bActive)
            return true;
        if (pTouch->touchPoints().size() != 1)
        {
            resetPull();
            m_bActive = false;
            releaseFeedback();
            pScroller->stop();
            return true;
        }
        const auto oPoint = pTouch->touchPoints().front();
        if (oPoint.id() != m_nId)
        {
            resetPull();
            m_bActive = false;
            releaseFeedback();
            pScroller->stop();
            return true;
        }
        const auto nTimestamp = inputTimestamp(pTouch);
        if ((oPoint.pos() - m_oStart).manhattanLength() > 12)
        {
            m_bMoved = true;
            releaseFeedback();
        }
        const bool bPulled = updatePull(oPoint.pos(), nTimestamp);
        if (pEvent->type() == QEvent::TouchUpdate)
        {
            if (!bPulled && !m_bPaged)
                moveScroll(oPoint.pos(), nTimestamp);
        }
        else
        {
            const bool bRefresh = m_bPulling && m_fPullAmount >= 1 && !m_bPullBlocked && canPull();
            const bool bConsumed = m_bPulling || bPulled;
            m_bActive = false;
            releaseFeedback();
            if (bConsumed)
            {
                pScroller->stop();
                resetPull();
                if (bRefresh && m_fnPullRefresh)
                    m_fnPullRefresh();
                return true;
            }
            resetPull();
            // Release coordinates can jump as the contact patch lifts. The last
            // move carries the intended direction; a stationary release still lets
            // QScroller decay velocity using the actual release timestamp.
            if (!m_bPaged)
            {
                pScroller->handleInput(QScroller::InputRelease, m_oScrollPoint, nTimestamp);
            }
            if (m_bPaged && m_bMoved && m_fnTurn)
            {
                const auto oDelta = oPoint.pos() - m_oStart;
                const auto fDistance =
                    qAbs(oDelta.x()) > qAbs(oDelta.y()) ? oDelta.x() : oDelta.y();
                if (qAbs(fDistance) >= 40 && nTimestamp < 1600)
                    m_fnTurn(fDistance < 0 ? 1 : -1);
            }
            if (!m_bMoved && !m_bInterrupted && nTimestamp < 600)
            {
                if (m_fnTap)
                    m_fnTap(oPoint.pos().toPoint());
                else
                    for (auto pWidget = m_pArea->viewport()->childAt(oPoint.pos().toPoint());
                         pWidget && pWidget != m_pArea->viewport();
                         pWidget = pWidget->parentWidget())
                    {
                        if (auto pButton = qobject_cast<QAbstractButton*>(pWidget))
                        {
                            if (pButton->isEnabled())
                                pButton->click();
                            break;
                        }
                        if (auto pInput = qobject_cast<QLineEdit*>(pWidget))
                        {
                            pInput->setFocus(Qt::MouseFocusReason);
                            pInput->setCursorPosition(pInput->cursorPositionAt(
                                pInput->mapFrom(m_pArea->viewport(), oPoint.pos().toPoint())));
                            QGuiApplication::inputMethod()->show();
                            break;
                        }
                    }
            }
        }
        return true;
    }
} // namespace Wenku8::Reader
