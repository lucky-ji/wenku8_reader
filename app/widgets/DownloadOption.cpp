// SPDX-License-Identifier: GPL-2.0-only
#include "DownloadOption.h"
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
namespace Wenku8::Reader
{
    DownloadOption::DownloadOption(QWidget* pParent) : QCheckBox("包含插图", pParent)
    {
        setObjectName("includeIllustrations");
        setAccessibleName("包含插图");
        setToolTip("下载小说时一并保存插图");
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFocusPolicy(Qt::StrongFocus);
        // The surrounding theme applies a general checkbox padding rule. This
        // control owns its geometry and drawing, so do not inherit style offsets.
        setStyleSheet("QCheckBox#includeIllustrations { padding:0; margin:0; border:0; "
                      "background:transparent; }");
        setTouchUi(false);
    }
    QFont DownloadOption::optionFont() const
    {
        auto oResult = font();
        oResult.setPixelSize(m_bTouchUi ? 14 : 13);
        return oResult;
    }
    void DownloadOption::setTouchUi(bool bEnabled)
    {
        const int nTarget = bEnabled ? 48 : 36;
        if (m_bTouchUi == bEnabled && minimumHeight() == nTarget && maximumHeight() == nTarget)
            return;
        m_bTouchUi = bEnabled;
        setFixedHeight(nTarget);
        updateGeometry();
        update();
    }
    void DownloadOption::setTheme(Theme eTheme)
    {
        m_eTheme = eTheme;
        update();
    }
    QSize DownloadOption::sizeHint() const
    {
        const auto oMetrics = QFontMetrics(optionFont());
        const int nMargin = m_bTouchUi ? 14 : 12, nIndicator = m_bTouchUi ? 22 : 18;
        return {2 * nMargin + nIndicator + 10 + oMetrics.horizontalAdvance(text()),
                m_bTouchUi ? 48 : 36};
    }
    QSize DownloadOption::minimumSizeHint() const
    {
        return sizeHint();
    }
    bool DownloadOption::hitButton(const QPoint& oPoint) const
    {
        return rect().contains(oPoint);
    }
    void DownloadOption::paintEvent(QPaintEvent*)
    {
        const auto oC = colors(m_eTheme);
        const bool bEnabled = isEnabled(), bHover = bEnabled && !m_bTouchUi && underMouse(),
                   bPressed = bEnabled && isDown();
        QPainter oP(this);
        oP.setRenderHint(QPainter::Antialiasing);
        oP.setFont(optionFont());
        const QRectF oSurface = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        QColor oFill = oC.m_oPanel;
        if (isChecked() || checkState() == Qt::PartiallyChecked)
            oFill = oC.m_oHover;
        if (bHover)
            oFill = oC.m_oHover;
        if (bPressed)
            oFill = oC.m_oSelection;
        if (!bEnabled)
            oFill = oC.m_oSection;
        oP.setPen(QPen(hasFocus() && bEnabled ? oC.m_oAccent : oC.m_oBorder,
                       hasFocus() && bEnabled ? 1.5 : 1));
        oP.setBrush(oFill);
        oP.drawRoundedRect(oSurface, m_bTouchUi ? 10 : 8, m_bTouchUi ? 10 : 8);
        const int nMargin = m_bTouchUi ? 14 : 12, nEdge = m_bTouchUi ? 22 : 18;
        QRect oIndicator = QStyle::visualRect(layoutDirection(), rect(),
                                              QRect(nMargin, (height() - nEdge) / 2, nEdge, nEdge));
        const bool bSelected = checkState() != Qt::Unchecked;
        QColor oBorder = bSelected ? oC.m_oAccent : oC.m_oMuted,
               oBackground = bSelected ? oC.m_oAccent : oC.m_oPanel;
        if (!bEnabled)
        {
            oBorder = oC.m_oDisabled;
            oBackground = bSelected ? oC.m_oDisabled : oC.m_oSection;
        }
        oP.setPen(QPen(oBorder, 1.5));
        oP.setBrush(oBackground);
        oP.drawRoundedRect(QRectF(oIndicator).adjusted(.75, .75, -.75, -.75), 5, 5);
        if (bSelected)
        {
            const QRectF oBox = oIndicator;
            QPen oMark(bEnabled ? oC.m_oAccentText : oC.m_oPanel, nEdge >= 22 ? 2.1 : 1.9,
                       Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            oP.setPen(oMark);
            oP.setBrush(Qt::NoBrush);
            if (checkState() == Qt::PartiallyChecked)
                oP.drawLine(QPointF(oBox.left() + nEdge * .27, oBox.center().y()),
                            QPointF(oBox.left() + nEdge * .73, oBox.center().y()));
            else
            {
                QPainterPath oTick;
                oTick.moveTo(oBox.left() + nEdge * .24, oBox.top() + nEdge * .51);
                oTick.lineTo(oBox.left() + nEdge * .43, oBox.top() + nEdge * .70);
                oTick.lineTo(oBox.left() + nEdge * .77, oBox.top() + nEdge * .31);
                oP.drawPath(oTick);
            }
        }
        const int nTextX = nMargin + nEdge + 10;
        auto oTextArea =
            QStyle::visualRect(layoutDirection(), rect(),
                               QRect(nTextX, 0, qMax(0, width() - nTextX - nMargin), height()));
        oP.setPen(bEnabled ? oC.m_oText : oC.m_oDisabled);
        oP.drawText(
            oTextArea,
            Qt::AlignVCenter |
                (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft),
            QFontMetrics(optionFont()).elidedText(text(), Qt::ElideRight, oTextArea.width()));
    }
    void DownloadOption::enterEvent(QEvent* pEvent)
    {
        QCheckBox::enterEvent(pEvent);
        update();
    }
    void DownloadOption::leaveEvent(QEvent* pEvent)
    {
        QCheckBox::leaveEvent(pEvent);
        update();
    }
    void DownloadOption::changeEvent(QEvent* pEvent)
    {
        QCheckBox::changeEvent(pEvent);
        if (pEvent->type() == QEvent::FontChange)
            updateGeometry();
        if (pEvent->type() == QEvent::EnabledChange)
            setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
} // namespace Wenku8::Reader
