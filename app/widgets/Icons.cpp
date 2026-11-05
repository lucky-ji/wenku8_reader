// SPDX-License-Identifier: GPL-2.0-only
#include "Icons.h"
#include <QAbstractButton>
#include <QIconEngine>
#include <QSvgRenderer>
#include <QPainter>
#include <QPalette>
#include <QFile>
#include <QCache>
#include <QResource>
#include <QVariant>
// Keep resource initialization outside the application namespace: the RCC
// object lives in a static library and otherwise may be discarded by the linker.
static void initializeWenkuIcons()
{
    static const bool bDone = []
    {
        Q_INIT_RESOURCE(icons);
        return true;
    }();
    Q_UNUSED(bDone);
}
namespace Wenku8::Reader
{
    namespace
    {
        const char* vNames[] = {"library",      "discover",
                                "download",     "search",
                                "refresh",      "sign-in",
                                "book",         "list",
                                "left",         "right",
                                "text-smaller", "text-larger",
                                "bookmark",     "restore-bookmark",
                                "cancel",       "sun",
                                "moon",         "leaf",
                                "shield",       "history",
                                "shelf-add",    "fullscreen",
                                "profile"};
        QSvgRenderer* renderer(Icon eIcon, const QColor& oColor)
        {
            initializeWenkuIcons();
            static QCache<QString, QSvgRenderer> vCache(256);
            const auto strName = QString::fromLatin1(vNames[int(eIcon)]);
            const auto strKey = strName + oColor.name();
            if (auto pCached = vCache.object(strKey))
                return pCached;
            QFile oFile(":/wenku8/icons/" + strName + ".svg");
            if (!oFile.open(QIODevice::ReadOnly))
                return nullptr;
            auto baSvg = oFile.readAll();
            baSvg.replace("#000000", oColor.name().toLatin1());
            auto pResult = new QSvgRenderer(baSvg);
            if (!pResult->isValid())
            {
                delete pResult;
                return nullptr;
            }
            vCache.insert(strKey, pResult);
            return pResult;
        }
        class SvgEngine final : public QIconEngine
        {
            Icon m_eIcon;
            QColor m_oNormal, m_oDisabled, m_oActive;

          public:
            SvgEngine(Icon eIcon, QColor oNormal, QColor oDisabled, QColor oActive)
                : m_eIcon(eIcon), m_oNormal(oNormal), m_oDisabled(oDisabled), m_oActive(oActive)
            {
            }
            QIconEngine* clone() const override
            {
                return new SvgEngine(*this);
            }
            QString key() const override
            {
                return "Wenku8Svg";
            }
            void paint(QPainter* pPainter, const QRect& oRect, QIcon::Mode eMode,
                       QIcon::State eState) override
            {
                auto pSvg = renderer(m_eIcon, eMode == QIcon::Disabled ? m_oDisabled
                                              : (eState == QIcon::On || eMode == QIcon::Selected)
                                                  ? m_oActive
                                                  : m_oNormal);
                if (!pSvg)
                    return;
                pPainter->save();
                pPainter->setRenderHint(QPainter::Antialiasing);
                pSvg->render(pPainter, QRectF(oRect));
                pPainter->restore();
            }
            QPixmap pixmap(const QSize& oSize, QIcon::Mode eMode, QIcon::State eState) override
            {
                QPixmap oResult(oSize);
                oResult.fill(Qt::transparent);
                QPainter oPainter(&oResult);
                paint(&oPainter, QRect(QPoint(), oSize), eMode, eState);
                return oResult;
            }
            void virtual_hook(int nId, void* pData) override
            {
                if (nId == ScaledPixmapHook)
                {
                    auto pArg = static_cast<ScaledPixmapArgument*>(pData);
                    pArg->pixmap = pixmap(pArg->size, pArg->mode, pArg->state);
                    pArg->pixmap.setDevicePixelRatio(pArg->scale);
                    return;
                }
                QIconEngine::virtual_hook(nId, pData);
            }
        };
    } // namespace
    QIcon makeIcon(Icon eIcon, const QColor& oNormal, const QColor& oDisabled,
                   const QColor& oActive)
    {
        return QIcon(new SvgEngine(eIcon, oNormal, oDisabled, oActive));
    }
    void setButtonIcon(QAbstractButton* pButton, Icon eIcon)
    {
        pButton->setProperty("wenkuIcon", int(eIcon));
        pButton->setIconSize(QSize(20, 20));
        const auto oP = pButton->palette();
        pButton->setIcon(makeIcon(eIcon, oP.color(QPalette::ButtonText),
                                  oP.color(QPalette::Disabled, QPalette::ButtonText),
                                  oP.color(QPalette::Highlight)));
    }
} // namespace Wenku8::Reader
