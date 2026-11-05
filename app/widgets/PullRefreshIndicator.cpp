// SPDX-License-Identifier: GPL-2.0-only
#include "PullRefreshIndicator.h"
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QHideEvent>
#include <QtMath>
#include <cmath>
namespace Wenku8::Reader
{
    PullRefreshIndicator::PullRefreshIndicator(QWidget* pParent) : QWidget(pParent)
    {
        setObjectName("pullRefreshIndicator");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
        setMaximumWidth(240);
        setFixedHeight(40);
        resize(sizeHint());
        m_oSpin.setInterval(33);
        connect(&m_oSpin, &QTimer::timeout, this,
                [this]
                {
                    update(QRect(12, 7, 30, 26));
                });
        m_oDismissal.setDuration(120);
        m_oDismissal.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_oDismissal, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& oValue)
                {
                    m_fOpacity = oValue.toReal();
                    update();
                });
        connect(&m_oDismissal, &QVariantAnimation::finished, this,
                [this]
                {
                    if (!m_bRefreshing && m_fProgress <= 0)
                    {
                        hide();
                        m_fOpacity = 1;
                    }
                });
        synchronize();
        hide();
    }
    void PullRefreshIndicator::setTheme(Theme eTheme)
    {
        m_eTheme = eTheme;
        update();
    }
    void PullRefreshIndicator::setProgress(qreal fProgress)
    {
        if (!std::isfinite(fProgress))
            fProgress = 0;
        m_fProgress = qBound(qreal(0), fProgress, qreal(1.5));
        synchronize();
        if (m_fProgress > 0 || m_bRefreshing)
        {
            m_oDismissal.stop();
            m_fOpacity = 1;
            show();
            raise();
            update();
        }
        else
            dismiss();
    }
    void PullRefreshIndicator::setRefreshing(bool bRefreshing)
    {
        m_bRefreshing = bRefreshing;
        synchronize();
        if (m_bRefreshing)
        {
            m_oDismissal.stop();
            m_fOpacity = 1;
            if (!m_oClock.isValid())
                m_oClock.start();
            show();
            raise();
            if (isVisible())
                m_oSpin.start();
            update();
        }
        else
        {
            m_oSpin.stop();
            if (m_fProgress <= 0)
                dismiss();
            else
                update();
        }
    }
    void PullRefreshIndicator::synchronize()
    {
        const auto pState = m_bRefreshing      ? "refreshing"
                            : m_fProgress >= 1 ? "armed"
                            : m_fProgress > 0  ? "pull"
                                               : "idle";
        setProperty("pullProgress", m_fProgress);
        setProperty("refreshing", m_bRefreshing);
        setProperty("pullState", pState);
        setAccessibleName(m_bRefreshing ? "正在刷新" : m_fProgress >= 1 ? "松开刷新" : "下拉刷新");
    }
    void PullRefreshIndicator::dismiss()
    {
        if (!isVisible())
        {
            m_oDismissal.stop();
            hide();
            m_fOpacity = 1;
            return;
        }
        if (m_oDismissal.state() == QAbstractAnimation::Running)
            return;
        m_oSpin.stop();
        m_oDismissal.setStartValue(m_fOpacity);
        m_oDismissal.setEndValue(0.0);
        m_oDismissal.start();
    }
    void PullRefreshIndicator::paintEvent(QPaintEvent*)
    {
        const auto oC = colors(m_eTheme);
        QPainter oPainter(this);
        oPainter.setRenderHint(QPainter::Antialiasing);
        oPainter.setOpacity(m_fOpacity);
        oPainter.translate(0, -8 * (1 - m_fOpacity));
        const QRectF oSurface = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        oPainter.setPen(QPen(oC.m_oBorder, 1));
        oPainter.setBrush(oC.m_oPanel);
        oPainter.drawRoundedRect(oSurface, 19, 19);
        const QColor oInk = m_bRefreshing || m_fProgress >= 1 ? oC.m_oAccent : oC.m_oMuted;
        const QPointF oCentre(27, height() / 2.0);
        oPainter.setPen(QPen(oInk, 2, Qt::SolidLine, Qt::RoundCap));
        oPainter.setBrush(Qt::NoBrush);
        if (m_bRefreshing)
        {
            const auto fSeconds = m_oClock.isValid() ? m_oClock.elapsed() / 1000.0 : 0.0;
            const auto fRotation = std::fmod(fSeconds * 260.0, 360.0);
            oPainter.drawArc(QRectF(oCentre - QPointF(8, 8), QSizeF(16, 16)),
                             qRound((90 - fRotation) * 16), -260 * 16);
        }
        else
        {
            oPainter.save();
            oPainter.translate(oCentre);
            oPainter.rotate(qMin(qreal(1), m_fProgress) * 180);
            QPainterPath oArrow;
            oArrow.moveTo(0, -7);
            oArrow.lineTo(0, 7);
            oArrow.moveTo(-5, 2);
            oArrow.lineTo(0, 7);
            oArrow.lineTo(5, 2);
            oPainter.drawPath(oArrow);
            oPainter.restore();
        }
        auto oType = font();
        oType.setPixelSize(13);
        oType.setWeight(m_fProgress >= 1 || m_bRefreshing ? QFont::DemiBold : QFont::Normal);
        oPainter.setFont(oType);
        oPainter.setPen(oC.m_oText);
        oPainter.drawText(QRect(48, 0, qMax(0, width() - 60), height()),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          m_bRefreshing      ? "正在刷新…"
                          : m_fProgress >= 1 ? "松开刷新"
                                             : "下拉刷新");
    }
    void PullRefreshIndicator::showEvent(QShowEvent* pEvent)
    {
        QWidget::showEvent(pEvent);
        if (m_bRefreshing)
        {
            if (!m_oClock.isValid())
                m_oClock.start();
            m_oSpin.start();
        }
    }
    void PullRefreshIndicator::hideEvent(QHideEvent* pEvent)
    {
        m_oSpin.stop();
        m_oDismissal.stop();
        QWidget::hideEvent(pEvent);
    }
} // namespace Wenku8::Reader
