// SPDX-License-Identifier: GPL-2.0-only
#include "ThemeSelector.h"
#include "Icons.h"
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QHideEvent>
namespace Wenku8::Reader
{
    namespace
    {
        class ThemeOption final : public QAbstractButton
        {
          public:
            Theme m_eTheme = Theme::Light;
            int m_nIndex = 0;
            explicit ThemeOption(int nI, QWidget* pParent) : QAbstractButton(pParent), m_nIndex(nI)
            {
                setText(QStringList{"明亮", "暗色", "护眼"}[nI]);
                setCheckable(true);
                setFocusPolicy(Qt::StrongFocus);
                setCursor(Qt::PointingHandCursor);
                setFixedHeight(58);
                setAccessibleName(text());
            }

          protected:
            void enterEvent(QEvent* pEvent) override
            {
                QAbstractButton::enterEvent(pEvent);
                update();
            }
            void leaveEvent(QEvent* pEvent) override
            {
                QAbstractButton::leaveEvent(pEvent);
                update();
            }
            void focusInEvent(QFocusEvent* pEvent) override
            {
                QAbstractButton::focusInEvent(pEvent);
                update();
            }
            void focusOutEvent(QFocusEvent* pEvent) override
            {
                QAbstractButton::focusOutEvent(pEvent);
                update();
            }
            void paintEvent(QPaintEvent*) override
            {
                const auto oC = colors(m_eTheme);
                QPainter oP(this);
                oP.setRenderHint(QPainter::Antialiasing);
                auto oR = rect().adjusted(1, 1, -1, -1);
                const bool bSelected = isChecked(), bHover = underMouse();
                QColor oInk = bSelected ? oC.m_oSelectionText : oC.m_oText;
                oP.setPen(bSelected ? QPen(oC.m_oSelectionText, 1) : QPen(Qt::NoPen));
                oP.setBrush(bSelected ? oC.m_oSelection : bHover ? oC.m_oHover : Qt::transparent);
                oP.drawRoundedRect(oR, 10, 10);
                makeIcon(m_nIndex == 0   ? Icon::Sun
                         : m_nIndex == 1 ? Icon::Moon
                                         : Icon::Leaf,
                         oInk, oC.m_oDisabled, oInk)
                    .paint(&oP, QRect(14, 19, 20, 20));
                QFont oF = font();
                oF.setPointSize(10);
                oF.setBold(bSelected);
                oP.setFont(oF);
                oP.setPen(oInk);
                oP.drawText(QRect(46, 9, width() - 82, 22), Qt::AlignVCenter, text());
                oF.setPointSize(8);
                oF.setBold(false);
                oP.setFont(oF);
                oP.setPen(bSelected ? oC.m_oSelectionText : oC.m_oMuted);
                oP.drawText(
                    QRect(46, 31, width() - 82, 18), Qt::AlignVCenter,
                    QStringList{"清晰明亮的界面", "适合低光环境", "温润的纸色背景"}[m_nIndex]);
                if (bSelected)
                {
                    oP.setPen(QPen(oInk, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    QPainterPath oTick;
                    oTick.moveTo(width() - 29, 29);
                    oTick.lineTo(width() - 25, 33);
                    oTick.lineTo(width() - 18, 24);
                    oP.drawPath(oTick);
                }
            }
        };
    } // namespace
    class ThemePopup final : public QWidget
    {
        ThemeSelector* m_pOwner;
        Theme m_eTheme = Theme::Light;
        QVector<ThemeOption*> m_vOptions;

      public:
        explicit ThemePopup(ThemeSelector* pOwner)
            : QWidget(pOwner, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
              m_pOwner(pOwner)
        {
            setObjectName("themePopup");
            setAttribute(Qt::WA_TranslucentBackground);
            setAttribute(Qt::WA_StyledBackground, false);
            setAutoFillBackground(false);
            auto pLayout = new QVBoxLayout(this);
            pLayout->setContentsMargins(16, 16, 16, 16);
            pLayout->setSpacing(4);
            for (int nI = 0; nI < 3; ++nI)
            {
                auto pRow = new ThemeOption(nI, this);
                pRow->installEventFilter(this);
                m_vOptions.append(pRow);
                pLayout->addWidget(pRow);
                connect(pRow, &QAbstractButton::clicked, this,
                        [this, nI]
                        {
                            m_pOwner->setCurrentIndex(nI);
                            m_pOwner->hidePopup();
                            m_pOwner->setFocus(Qt::PopupFocusReason);
                        });
            }
        }
        void prepare(Theme eTheme, int nSelected)
        {
            m_eTheme = eTheme;
            for (auto pRow : m_vOptions)
            {
                pRow->m_eTheme = eTheme;
                pRow->setChecked(pRow->m_nIndex == nSelected);
                pRow->update();
            }
            update();
        }
        void focusSelection(int nSelected)
        {
            m_vOptions[qBound(0, nSelected, 2)]->setFocus(Qt::PopupFocusReason);
        }

      protected:
        bool eventFilter(QObject* pWatched, QEvent* pEvent) override
        {
            if (pEvent->type() == QEvent::ShortcutOverride)
            {
                auto pKey = static_cast<QKeyEvent*>(pEvent);
                if (pKey->key() == Qt::Key_Escape || pKey->key() == Qt::Key_Up ||
                    pKey->key() == Qt::Key_Down || pKey->key() == Qt::Key_Return ||
                    pKey->key() == Qt::Key_Enter)
                {
                    pKey->accept();
                    return true;
                }
            }
            if (pEvent->type() != QEvent::KeyPress)
                return QWidget::eventFilter(pWatched, pEvent);
            auto pKey = static_cast<QKeyEvent*>(pEvent);
            auto pRow = static_cast<ThemeOption*>(pWatched);
            int nIndex = pRow->m_nIndex;
            if (pKey->key() == Qt::Key_Escape)
            {
                m_pOwner->hidePopup();
                m_pOwner->setFocus(Qt::PopupFocusReason);
                return true;
            }
            if (pKey->key() == Qt::Key_Up || pKey->key() == Qt::Key_Down)
            {
                m_vOptions[(nIndex + (pKey->key() == Qt::Key_Down ? 1 : 2)) % 3]->setFocus();
                return true;
            }
            if (pKey->key() == Qt::Key_Return || pKey->key() == Qt::Key_Enter)
            {
                pRow->click();
                return true;
            }
            return QWidget::eventFilter(pWatched, pEvent);
        }
        void hideEvent(QHideEvent* pE) override
        {
            QWidget::hideEvent(pE);
            m_pOwner->update();
        }
        void paintEvent(QPaintEvent*) override
        {
            QPainter oP(this);
            oP.setRenderHint(QPainter::Antialiasing);
            const auto oC = colors(m_eTheme);
            oP.setPen(Qt::NoPen);
            for (int nInset = 2; nInset < 8; ++nInset)
            {
                oP.setBrush(QColor(0, 0, 0, m_eTheme == Theme::Dark ? 9 : 4));
                oP.drawRoundedRect(rect().adjusted(nInset, nInset + 2, -nInset, -nInset),
                                   18 - nInset / 2, 18 - nInset / 2);
            }
            oP.setPen(QPen(oC.m_oBorder, 1));
            oP.setBrush(oC.m_oPanel);
            oP.drawRoundedRect(rect().adjusted(8, 8, -8, -8), 14, 14);
        }
    };
    ThemeSelector::ThemeSelector(QWidget* pParent) : QComboBox(pParent)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(42);
        setAccessibleName("外观主题");
    }
    ThemeSelector::~ThemeSelector() = default;
    void ThemeSelector::setTheme(Theme eTheme)
    {
        m_eTheme = eTheme;
        if (m_pPopup)
            m_pPopup->prepare(eTheme, currentIndex());
        update();
    }
    void ThemeSelector::showPopup()
    {
        if (!m_pPopup)
            m_pPopup = new ThemePopup(this);
        m_pPopup->prepare(m_eTheme, currentIndex());
        auto pScreen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
        if (!pScreen)
            pScreen = QGuiApplication::primaryScreen();
        QRect oAvailable = pScreen ? pScreen->availableGeometry() : QRect(0, 0, 1920, 1080);
        int nW = qMin(qMax(width() + 16, 280), oAvailable.width() - 16), nH = 214;
        QPoint oPoint = mapToGlobal(QPoint(-8, height() + 4));
        if (oPoint.y() + nH > oAvailable.bottom())
            oPoint.setY(mapToGlobal(QPoint(0, 0)).y() - nH - 4);
        oPoint.setX(qBound(oAvailable.left() + 8, oPoint.x(),
                           oAvailable.left() + oAvailable.width() - nW - 8));
        oPoint.setY(qMax(oAvailable.top() + 8, oPoint.y()));
        m_pPopup->setGeometry(QRect(oPoint, QSize(nW, nH)));
        m_pPopup->show();
        m_pPopup->raise();
        m_pPopup->focusSelection(currentIndex());
        update();
    }
    void ThemeSelector::hidePopup()
    {
        if (m_pPopup)
            m_pPopup->hide();
        QComboBox::hidePopup();
        update();
    }
    void ThemeSelector::paintEvent(QPaintEvent*)
    {
        auto oC = colors(m_eTheme);
        QPainter oP(this);
        oP.setRenderHint(QPainter::Antialiasing);
        bool bActive = underMouse() || (m_pPopup && m_pPopup->isVisible());
        oP.setBrush(bActive ? oC.m_oHover : oC.m_oPanel);
        oP.setPen(QPen(hasFocus() ? oC.m_oSelectionText
                       : bActive  ? oC.m_oMuted
                                  : oC.m_oBorder,
                       hasFocus() ? 1.5 : 1));
        oP.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
        itemIcon(currentIndex()).paint(&oP, QRect(13, (height() - 18) / 2, 18, 18));
        oP.setPen(oC.m_oText);
        oP.drawText(QRect(41, 0, width() - 72, height()), Qt::AlignVCenter | Qt::AlignLeft,
                    currentText());
        oP.setPen(QPen(oC.m_oMuted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        int nX = width() - 20, nY = height() / 2;
        QPainterPath oArrow;
        oArrow.moveTo(nX - 4, nY - 2);
        oArrow.lineTo(nX, nY + 2);
        oArrow.lineTo(nX + 4, nY - 2);
        oP.drawPath(oArrow);
    }
} // namespace Wenku8::Reader
