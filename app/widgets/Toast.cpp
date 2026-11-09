// SPDX-License-Identifier: GPL-2.0-only
#include "Toast.h"
#include <QEvent>
#include <QPainter>
#include <QFontMetrics>
namespace Wenku8::Reader
{
    Toast::Toast(QWidget* pParent) : QWidget(pParent)
    {
        setObjectName("downloadToast");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
        pParent->installEventFilter(this);
        m_oTimer.setSingleShot(true);
        connect(&m_oTimer, &QTimer::timeout, this, &QWidget::hide);
        hide();
    }
    void Toast::showText(const QString& strText, Theme eTheme)
    {
        m_strText = strText;
        m_eTheme = eTheme;
        reposition();
        show();
        raise();
        update();
        m_oTimer.start(4000);
    }
    bool Toast::eventFilter(QObject* pObject, QEvent* pEvent)
    {
        if (pObject == parentWidget() && pEvent->type() == QEvent::Resize)
        {
            reposition();
        }
        return QWidget::eventFilter(pObject, pEvent);
    }
    void Toast::reposition()
    {
        const int nAvailable = qMax(80, parentWidget()->width() - 32);
        const int nWidth = qMin(560, nAvailable);
        const QRect oText = fontMetrics().boundingRect(
            QRect(0, 0, nWidth - 36, 240), Qt::TextWordWrap | Qt::AlignCenter, m_strText);
        const int nActualWidth = qMin(nWidth, qMax(160, oText.width() + 36));
        const int nHeight = qMin(260, qMax(44, oText.height() + 24));
        const int nBottom = parentWidget()->width() < 700 ? 100 : 48;
        setGeometry((parentWidget()->width() - nActualWidth) / 2,
                    qMax(8, parentWidget()->height() - nBottom - nHeight), nActualWidth, nHeight);
    }
    void Toast::paintEvent(QPaintEvent*)
    {
        const auto oColors = colors(m_eTheme);
        QPainter oPainter(this);
        oPainter.setRenderHint(QPainter::Antialiasing);
        oPainter.setPen(oColors.m_oBorder);
        oPainter.setBrush(oColors.m_oText);
        oPainter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 14, 14);
        oPainter.setPen(oColors.m_oPanel);
        oPainter.drawText(rect().adjusted(18, 10, -18, -10),
                          Qt::AlignCenter | Qt::TextWordWrap, m_strText);
    }
}
