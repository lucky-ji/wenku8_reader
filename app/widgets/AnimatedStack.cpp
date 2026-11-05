// SPDX-License-Identifier: GPL-2.0-only
#include "AnimatedStack.h"
#include <QPainter>
#include <QVariantAnimation>
#include <QLayout>
namespace Wenku8::Reader
{
    class PageTransition final : public QWidget
    {
      public:
        QPixmap m_oBefore, m_oAfter;
        qreal m_fProgress = 0;
        bool m_bBack = false;
        explicit PageTransition(QWidget* pParent) : QWidget(pParent)
        {
            setAttribute(Qt::WA_OpaquePaintEvent);
            setFocusPolicy(Qt::NoFocus);
            setObjectName("pageTransition");
        }

      protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter oP(this);
            oP.fillRect(rect(), palette().window());
            int nDistance = width();
            oP.drawPixmap(QPoint(int((m_bBack ? 1 : -1) * m_fProgress * nDistance), 0), m_oBefore);
            oP.drawPixmap(QPoint(int((m_bBack ? -1 : 1) * (1 - m_fProgress) * nDistance), 0),
                          m_oAfter);
        }
    };
    void AnimatedStack::finishTransition()
    {
        if (m_pAnimation)
        {
            m_pAnimation->disconnect(this);
            m_pAnimation->stop();
            m_pAnimation->deleteLater();
            m_pAnimation = nullptr;
        }
        delete m_pOverlay;
        m_pOverlay = nullptr;
    }
    void AnimatedStack::setMotionEnabled(bool bEnabled)
    {
        m_bMotion = bEnabled;
        if (!bEnabled)
            finishTransition();
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
        if (nIndex < 0 || nIndex >= count() || nIndex == currentIndex())
            return;
        finishTransition();
        QPixmap oBefore;
        const bool bAnimate =
            m_bMotion && isVisible() && currentWidget() && currentWidget()->isVisible();
        if (bAnimate)
            oBefore = currentWidget()->grab();
        QStackedWidget::setCurrentIndex(nIndex);
        if (!bAnimate)
            return;
        if (parentWidget() && parentWidget()->layout())
            parentWidget()->layout()->activate();
        if (layout())
            layout()->activate();
        if (currentWidget()->layout())
            currentWidget()->layout()->activate();
        m_pOverlay = new PageTransition(this);
        m_pOverlay->setGeometry(rect());
        m_pOverlay->m_oBefore = oBefore;
        m_pOverlay->m_oAfter = currentWidget()->grab();
        m_pOverlay->m_bBack = bBack;
        m_pOverlay->show();
        m_pOverlay->raise();
        m_pAnimation = new QVariantAnimation(this);
        m_pAnimation->setStartValue(0.0);
        m_pAnimation->setEndValue(1.0);
        m_pAnimation->setDuration(180);
        m_pAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_pAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& oValue)
                {
                    if (m_pOverlay)
                    {
                        m_pOverlay->m_fProgress = oValue.toReal();
                        m_pOverlay->update();
                    }
                });
        connect(m_pAnimation, &QVariantAnimation::finished, this,
                [this]
                {
                    finishTransition();
                });
        m_pAnimation->start();
    }
    void AnimatedStack::resizeEvent(QResizeEvent* pEvent)
    {
        finishTransition();
        QStackedWidget::resizeEvent(pEvent);
    }
} // namespace Wenku8::Reader
