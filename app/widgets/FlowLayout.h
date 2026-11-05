// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QLayout>
#include <QWidget>
namespace Wenku8::Reader
{
    // Wrap native controls without a horizontal scrollbar on narrow screens.
    class FlowLayout final : public QLayout
    {
      public:
        explicit FlowLayout(int nGap = 8) : m_nGap(nGap)
        {
            setContentsMargins(0, 0, 0, 0);
        }
        ~FlowLayout() override
        {
            while (auto pItem = takeAt(0))
                delete pItem;
        }
        void addItem(QLayoutItem* pItem) override
        {
            m_vItems.append(pItem);
        }
        int count() const override
        {
            return m_vItems.size();
        }
        QLayoutItem* itemAt(int nI) const override
        {
            return m_vItems.value(nI);
        }
        QLayoutItem* takeAt(int nI) override
        {
            return nI >= 0 && nI < m_vItems.size() ? m_vItems.takeAt(nI) : nullptr;
        }
        Qt::Orientations expandingDirections() const override
        {
            return {};
        }
        bool hasHeightForWidth() const override
        {
            return true;
        }
        int heightForWidth(int nW) const override
        {
            return arrange(QRect(0, 0, nW, 0), false);
        }
        QSize sizeHint() const override
        {
            return minimumSize();
        }
        QSize minimumSize() const override
        {
            QSize oResult;
            for (auto pItem : m_vItems)
                if (!pItem->isEmpty())
                    oResult = oResult.expandedTo(pItem->minimumSize());
            return oResult;
        }
        void setGeometry(const QRect& oR) override
        {
            QLayout::setGeometry(oR);
            arrange(oR, true);
        }

      private:
        QList<QLayoutItem*> m_vItems;
        int m_nGap;
        int arrange(const QRect& oR, bool bPlace) const
        {
            int nX = oR.x(), nY = oR.y(), nLine = 0;
            for (auto pItem : m_vItems)
            {
                if (pItem->isEmpty())
                    continue;
                auto oSize = pItem->sizeHint();
                oSize.setWidth(qMin(oSize.width(), oR.width()));
                if (nX > oR.x() && nX + oSize.width() > oR.x() + oR.width())
                {
                    nX = oR.x();
                    nY += nLine + m_nGap;
                    nLine = 0;
                }
                if (bPlace)
                    pItem->setGeometry(QRect(QPoint(nX, nY), oSize));
                nX += oSize.width() + m_nGap;
                nLine = qMax(nLine, oSize.height());
            }
            return nY - oR.y() + nLine;
        }
    };
} // namespace Wenku8::Reader
