// SPDX-License-Identifier: GPL-2.0-only
#include "AnimatedStack.h"
#include <QApplication>
#include <QEvent>
#include <QVariantAnimation>
#include <QTimer>
namespace Wenku8::Reader
{
    void AnimatedStack::finishTransition()
    {
        ++m_nTransitionGeneration;
        if (m_pAnimation)
        {
            m_pAnimation->disconnect(this);
            m_pAnimation->stop();
            m_pAnimation->deleteLater();
            m_pAnimation = nullptr;
        }
        if (m_pAnimatedPage)
        {
            // Layout may have resized the page while motion was running.
            // Restore only our translation, never an obsolete width or height.
            m_pAnimatedPage->move(m_oRestPosition);
            m_pAnimatedPage.clear();
        }
    }
    void AnimatedStack::connectTransitionHooks()
    {
        if (m_bHooksConnected)
        {
            return;
        }
        m_bHooksConnected = true;
        connect(this, &QStackedWidget::currentChanged, this, [this]
        {
            if (m_pAnimatedPage && m_pAnimatedPage != currentWidget())
            {
                finishTransition();
            }
        });
        connect(this, &QStackedWidget::widgetRemoved, this, [this]
        {
            if (!m_pAnimatedPage || indexOf(m_pAnimatedPage) < 0)
            {
                finishTransition();
            }
        });
        if (qApp)
        {
            connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState eState)
                {
                    if (eState != Qt::ApplicationActive)
                    {
                        finishTransition();
                    }
                });
        }
    }
    void AnimatedStack::setMotionEnabled(bool bEnabled)
    {
        m_bMotion = bEnabled;
        if (!bEnabled)
        {
            finishTransition();
        }
    }
    void AnimatedStack::setCurrentIndex(int nIndex)
    {
        changePage(nIndex, false);
    }
    void AnimatedStack::returnTo(int nIndex)
    {
        changePage(nIndex, true);
    }
    void AnimatedStack::changePage(int nIndex, bool bBack)
    {
        if (nIndex < 0 || nIndex >= count())
        {
            return;
        }
        // Repeated navigation also settles an interrupted animation.
        if (nIndex == currentIndex())
        {
            finishTransition();
            return;
        }
        finishTransition();
        connectTransitionHooks();
        const bool bAnimate = m_bMotion && isVisible() && currentWidget() &&
                              currentWidget()->isVisible();
        QStackedWidget::setCurrentIndex(nIndex);
        if (!bAnimate || !currentWidget())
        {
            return;
        }
        // Animate the live page by a small distance: no full-page screenshots,
        // graphics effects or input-blocking overlay are needed at any DPI.
        m_pAnimatedPage = currentWidget();
        m_oRestPosition = m_pAnimatedPage->pos();
        const int nDistance = (bBack ? -1 : 1) * qBound(12, width() / 20, 24);
        m_pAnimatedPage->move(m_oRestPosition + QPoint(nDistance, 0));
        m_pAnimation = new QVariantAnimation(this);
        m_pAnimation->setStartValue(0.0);
        m_pAnimation->setEndValue(1.0);
        m_pAnimation->setDuration(160);
        m_pAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_pAnimation, &QVariantAnimation::valueChanged, this,
            [this, nDistance](const QVariant& oValue)
            {
                if (m_pAnimatedPage && m_pAnimatedPage == currentWidget())
                {
                    m_pAnimatedPage->move(m_oRestPosition +
                        QPoint(qRound(nDistance * (1.0 - oValue.toReal())), 0));
                }
                else
                {
                    finishTransition();
                }
            });
        connect(m_pAnimation, &QVariantAnimation::finished, this,
            [this]
            {
                finishTransition();
            });
        const quint64 nGeneration = m_nTransitionGeneration;
        // Animation clocks can pause during platform state changes. The live page
        // remains interactive throughout; this also bounds an unfinished offset.
        QTimer::singleShot(350, this, [this, nGeneration]
        {
            if (m_nTransitionGeneration == nGeneration)
            {
                finishTransition();
            }
        });
        m_pAnimation->start();
    }
    bool AnimatedStack::event(QEvent* pEvent)
    {
        if (pEvent->type() == QEvent::Hide || pEvent->type() == QEvent::WindowDeactivate)
        {
            finishTransition();
        }
        return QStackedWidget::event(pEvent);
    }
    void AnimatedStack::resizeEvent(QResizeEvent* pEvent)
    {
        finishTransition();
        QStackedWidget::resizeEvent(pEvent);
    }
} // namespace Wenku8::Reader
