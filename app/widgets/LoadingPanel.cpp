// SPDX-License-Identifier: GPL-2.0-only
#include "LoadingPanel.h"
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QDynamicPropertyChangeEvent>
#include <QVariant>
#include <algorithm>
#include <cmath>
namespace Wenku8::Reader
{
    LoadingPanel::LoadingPanel(QWidget* pParent)
        : QWidget(pParent), m_pTitle(new QLabel(this)), m_pMessage(new QLabel(this)),
          m_pProgressText(new QLabel(this)), m_pCancel(new QPushButton("取消加载", this))
    {
        setObjectName("loadingOverlay");
        setFocusPolicy(Qt::StrongFocus);
        // Keep Qt's ordinary touch-to-mouse synthesis for the cancel button.
        // The overlay covers the disabled shell, so no input reaches the reader.
        setAutoFillBackground(false);
        m_pTitle->setObjectName("loadingTitle");
        m_pMessage->setObjectName("loadingMessage");
        m_pProgressText->setObjectName("loadingProgress");
        m_pCancel->setObjectName("loadingCancel");
        for (auto pLabel : {m_pTitle, m_pMessage, m_pProgressText})
        {
            pLabel->setAlignment(Qt::AlignCenter);
            pLabel->setTextFormat(Qt::PlainText);
            pLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        m_pMessage->setWordWrap(true);
        m_pMessage->setText("正在准备内容，请稍候…");
        m_pCancel->setCursor(Qt::PointingHandCursor);
        connect(m_pCancel, &QPushButton::clicked, this,
                [this]
                {
                    if (m_bCancelable && !m_bCancelling && m_fnOnCancel)
                        m_fnOnCancel();
                });
        m_oAnimation.setInterval(33);
        connect(&m_oAnimation, &QTimer::timeout, this,
                [this]
                {
                    // Repaint just the vector animation, rather than the full reader below.
                    update(m_oRing.adjusted(-4, -4, 4, 4));
                    update(m_oStripe.adjusted(-1, -1, 1, 1));
                });
        setTheme(m_eTheme);
        setProgress(0, 0);
        hide();
    }
    bool LoadingPanel::startup() const
    {
        return property("startupMode").toBool();
    }
    void LoadingPanel::setTheme(Theme eTheme)
    {
        m_eTheme = eTheme;
        const auto oC = colors(eTheme);
        m_pTitle->setStyleSheet(
            QString("background:transparent;color:%1;font-size:19px;font-weight:600;border:0;")
                .arg(oC.m_oText.name()));
        m_pMessage->setStyleSheet(
            QString("background:transparent;color:%1;font-size:13px;border:0;")
                .arg(oC.m_oMuted.name()));
        m_pProgressText->setStyleSheet(
            QString("background:transparent;color:%1;font-size:12px;border:0;")
                .arg(oC.m_oMuted.name()));
        QString strCancelStyle =
            QString(
                "QPushButton{background:%1;color:%2;border:1px solid "
                "%3;border-radius:11px;padding:0;font-size:13px;}QPushButton:hover{background:%4;"
                "border-color:%5;}QPushButton:pressed{background:%6;}"
                "QPushButton:disabled{color:%7;background:%1;border-color:%3;}")
                .arg(oC.m_oPanel.name(), oC.m_oText.name(), oC.m_oBorder.name(), oC.m_oHover.name(),
                     oC.m_oAccent.name(), oC.m_oSelection.name(), oC.m_oDisabled.name());
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        strCancelStyle +=
            QString("QPushButton:focus{border:2px solid %1;}").arg(oC.m_oAccent.name());
#else
        strCancelStyle += "QPushButton{outline:0;}";
#endif
        m_pCancel->setStyleSheet(strCancelStyle);
        update();
    }
    void LoadingPanel::setMessage(const QString& strMessage)
    {
        m_pMessage->setText(strMessage);
    }
    void LoadingPanel::setProgress(qint64 nCurrent, qint64 nTotal)
    {
        m_nTotal = qMax(qint64(0), nTotal);
        m_nCurrent = m_nTotal > 0 ? qBound(qint64(0), nCurrent, m_nTotal) : 0;
        m_pProgressText->setText(
            m_nTotal > 0 ? QString("%1 / %2 · %3%")
                               .arg(m_nCurrent)
                               .arg(m_nTotal)
                               .arg(qRound(100.0 * double(m_nCurrent) / double(m_nTotal)))
                         : QString("请稍候"));
        update(m_oRing.adjusted(-4, -4, 4, 4));
        update(m_oStripe.adjusted(-1, -1, 1, 1));
    }
    void LoadingPanel::setCancelable(bool bValue)
    {
        m_bCancelable = bValue;
        arrange();
    }
    void LoadingPanel::setCancelling(bool bValue)
    {
        m_bCancelling = bValue;
        m_pCancel->setEnabled(!bValue);
        m_pCancel->setText(bValue ? "正在取消…" : "取消加载");
        m_pTitle->setText(startup() ? "轻小说文库" : bValue ? "正在取消" : "正在加载");
    }
    void LoadingPanel::arrange()
    {
        const bool bCanCancel = m_bCancelable && !startup();
        const int nCardWidth = qMax(160, qMin(360, width() - 32)),
                  nCardHeight = bCanCancel ? 298 : 244;
        m_oCard = QRect((width() - nCardWidth) / 2, qMax(12, (height() - nCardHeight) / 2),
                        nCardWidth, nCardHeight);
        m_oRing = QRect(m_oCard.center().x() - 24, m_oCard.top() + 30, 48, 48);
        m_pTitle->setText(startup() ? "轻小说文库" : m_bCancelling ? "正在取消" : "正在加载");
        m_pTitle->setGeometry(m_oCard.left() + 20, m_oCard.top() + 96, nCardWidth - 40, 30);
        m_pMessage->setGeometry(m_oCard.left() + 20, m_oCard.top() + 134, nCardWidth - 40, 48);
        m_pProgressText->setGeometry(m_oCard.left() + 20, m_oCard.top() + 190, nCardWidth - 40, 20);
        m_oStripe = QRect(m_oCard.left() + 28, m_oCard.top() + 222, nCardWidth - 56, 4);
        m_pCancel->setVisible(bCanCancel);
        m_pCancel->setGeometry(m_oCard.center().x() - 66, m_oCard.top() + 244, 132, 40);
        update();
    }
    void LoadingPanel::paintEvent(QPaintEvent*)
    {
        const auto oC = colors(m_eTheme);
        QPainter oPainter(this);
        oPainter.setRenderHint(QPainter::Antialiasing);
        if (startup())
            oPainter.fillRect(rect(), oC.m_oBackground);
        else
        {
            auto oVeil =
                m_eTheme == Theme::Dark ? QColor(8, 13, 20, 192) : QColor(oC.m_oBackground);
            if (m_eTheme != Theme::Dark)
                oVeil.setAlpha(224);
            oPainter.fillRect(rect(), oVeil);
        }
        oPainter.setPen(QPen(oC.m_oBorder, 1));
        oPainter.setBrush(oC.m_oPanel);
        oPainter.drawRoundedRect(QRectF(m_oCard).adjusted(.5, .5, -.5, -.5), 22, 22);
        const qreal fTime = m_oElapsed.isValid() ? m_oElapsed.elapsed() / 1000.0 : 0;
        const qreal fRotation = std::fmod(fTime * 240.0, 360.0);
        oPainter.setBrush(Qt::NoBrush);
        oPainter.setPen(QPen(oC.m_oSection, 4, Qt::SolidLine, Qt::RoundCap));
        oPainter.drawEllipse(QRectF(m_oRing));
        oPainter.setPen(QPen(oC.m_oAccent, 4, Qt::SolidLine, Qt::RoundCap));
        const qreal fFraction = m_nTotal > 0 ? double(m_nCurrent) / double(m_nTotal) : 0;
        if (m_nTotal > 0)
            oPainter.drawArc(QRectF(m_oRing), 90 * 16, -qRound(360 * 16 * fFraction));
        else
            oPainter.drawArc(QRectF(m_oRing), qRound((90 - fRotation) * 16),
                             -qRound((72 + std::sin(fTime * 2.4) * 36) * 16));
        QPainterPath oClip;
        oClip.addRoundedRect(QRectF(m_oStripe), 2, 2);
        oPainter.setClipPath(oClip);
        oPainter.fillRect(m_oStripe, oC.m_oSection);
        if (m_nTotal > 0)
        {
            QRectF oFill = m_oStripe;
            oFill.setWidth(oFill.width() * fFraction);
            oPainter.fillRect(oFill, oC.m_oAccent);
        }
        else
        {
            const qreal fPosition = std::fmod(fTime / 1.5, 1.0), fSegment = m_oStripe.width() * .38;
            QRectF oMoving(m_oStripe.left() - fSegment + fPosition * (m_oStripe.width() + fSegment),
                           m_oStripe.top(), fSegment, m_oStripe.height());
            QLinearGradient oGradient(oMoving.topLeft(), oMoving.topRight());
            auto oTransparent = oC.m_oAccent;
            oTransparent.setAlpha(0);
            oGradient.setColorAt(0, oTransparent);
            oGradient.setColorAt(.5, oC.m_oAccent);
            oGradient.setColorAt(1, oTransparent);
            oPainter.fillRect(oMoving, oGradient);
        }
    }
    void LoadingPanel::resizeEvent(QResizeEvent* pEvent)
    {
        QWidget::resizeEvent(pEvent);
        arrange();
    }
    void LoadingPanel::showEvent(QShowEvent* pEvent)
    {
        QWidget::showEvent(pEvent);
        arrange();
        m_oElapsed.restart();
        m_oAnimation.start();
        raise();
        setFocus(Qt::OtherFocusReason);
    }
    void LoadingPanel::hideEvent(QHideEvent* pEvent)
    {
        m_oAnimation.stop();
        QWidget::hideEvent(pEvent);
    }
    void LoadingPanel::keyPressEvent(QKeyEvent* pEvent)
    {
        if ((pEvent->key() == Qt::Key_Escape || pEvent->key() == Qt::Key_Back) && m_bCancelable &&
            !startup() && !m_bCancelling && m_fnOnCancel)
            m_fnOnCancel();
        pEvent->accept();
    }
    void LoadingPanel::wheelEvent(QWheelEvent* pEvent)
    {
        pEvent->accept();
    }
    void LoadingPanel::mousePressEvent(QMouseEvent* pEvent)
    {
        pEvent->accept();
    }
    bool LoadingPanel::event(QEvent* pEvent)
    {
        if (pEvent->type() == QEvent::DynamicPropertyChange &&
            static_cast<QDynamicPropertyChangeEvent*>(pEvent)->propertyName() == "startupMode")
            arrange();
        return QWidget::event(pEvent);
    }
} // namespace Wenku8::Reader
