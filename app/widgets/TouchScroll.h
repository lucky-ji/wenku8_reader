// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QObject>
#include <QPointF>
#include <QElapsedTimer>
#include <QPointer>
#include <functional>
class QAbstractScrollArea;
class QAbstractButton;
class QTouchEvent;
namespace Wenku8::Reader
{
    // Own the complete touch sequence: a tap activates once; a drag/cancel or an
    // interruption of inertia never activates an item or synthesizes mouse clicks.
    class TouchScroll final : public QObject
    {
      public:
        explicit TouchScroll(QAbstractScrollArea*, std::function<void(QPoint)> fnTap = {});
        void setEnabled(bool);
        bool enabled() const
        {
            return m_bEnabled;
        }
        void setPageGestures(bool bEnabled, std::function<void(int)> fnTurn = {});
        void setPressAction(std::function<void()> fnAction)
        {
            m_fnPress = std::move(fnAction);
        }
        // Optional edge gesture. Progress is damped distance / 72 logical px;
        // a raw 96px pull reaches the release threshold. The header owns its return
        // animation; this controller always reports zero before invoking refresh.
        void setPullToRefresh(std::function<bool()> fnAllowed,
                              std::function<void(qreal)> fnProgress,
                              std::function<void()> fnRefresh);

      protected:
        bool eventFilter(QObject*, QEvent*) override;

      private:
        QAbstractScrollArea* m_pArea;
        std::function<void(QPoint)> m_fnTap;
        bool m_bEnabled = false, m_bActive = false, m_bMoved = false, m_bInterrupted = false;
        int m_nId = -1;
        QPointF m_oStart;
        QElapsedTimer m_oTime;
        quint64 m_nTouchStartTimestamp = 0;
        qint64 m_nLastInputTimestamp = 0;
        QPointer<QAbstractButton> m_pPressed;
        bool m_bPaged = false;
        std::function<void(int)> m_fnTurn;
        std::function<void()> m_fnPress;
        void releaseFeedback();
        qint64 inputTimestamp(const QTouchEvent*);
        std::function<bool()> m_fnPullAllowed;
        std::function<void(qreal)> m_fnPullProgress;
        std::function<void()> m_fnPullRefresh;
        bool m_bPulling = false, m_bPullBlocked = false, m_bPullAtTop = false;
        qreal m_fPullAnchorY = 0, m_fPullAmount = 0;
        bool canPull() const;
        void resetPull();
        bool updatePull(const QPointF&, qint64 nTimestamp);
    };
} // namespace Wenku8::Reader
