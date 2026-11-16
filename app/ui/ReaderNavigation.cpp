// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AnimatedStack.h"
#include "TouchScroll.h"
#include "AndroidKeys.h"
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSignalBlocker>
#include <QGuiApplication>
#include <limits>
namespace Wenku8::Reader
{
    void ClientWindow::setReadingMode(ReadingMode eMode)
    {
        m_pReader->setReadingMode(eMode);
        m_pReaderTouch->setPageGestures(eMode == ReadingMode::Paged,
                                        [this](int nDirection)
                                        {
                                            turnReaderPage(nDirection);
                                        });
        for (auto pChoice : findChildren<QPushButton*>("readingModeChoice"))
            pChoice->setChecked(pChoice->property("modeIndex").toInt() == int(eMode));
        saveReaderPreferences();
        updateReaderFooter();
    }
    void ClientWindow::saveReaderPreferences()
    {
        m_oSettings.setValue("readingMode", int(m_pReader->readingMode()));
        m_oSettings.setValue("volumePageKeys", m_bVolumePaging);
        m_oSettings.setValue("readerKeepAwake", m_bKeepAwake);
    }
    void ClientWindow::updateReaderFooter()
    {
        if (m_nChapter < 0)
            return;
        updateReaderProgress();
        const bool bPaged = m_pReader->readingMode() == ReadingMode::Paged;
        m_pChapterLabel->setText(
            bPaged ? QString("章节 %1 / %2  ·  本章 %3 / %4 页")
                         .arg(m_nChapter + 1)
                         .arg(m_vFlat.size())
                         .arg(m_pReader->pageNumber())
                         .arg(m_pReader->pageCount())
                   : QString("章节 %1 / %2").arg(m_nChapter + 1).arg(m_vFlat.size()));
        m_pPrevious->setText(bPaged ? (mobileMode() ? "上页" : "上一页") : "上一章");
        m_pNext->setText(bPaged ? (mobileMode() ? "下页" : "下一页") : "下一章");
        m_pPrevious->setEnabled(m_pRead->isEnabled() &&
                                (m_nChapter > 0 || (bPaged && m_pReader->pageNumber() > 1)));
        m_pNext->setEnabled(m_pRead->isEnabled() &&
                            (m_nChapter + 1 < m_vFlat.size() ||
                             (bPaged && m_pReader->pageNumber() < m_pReader->pageCount())));
    }
    void ClientWindow::updateReaderProgress()
    {
        if (!m_pReaderProgress || m_pReaderProgress->isSliderDown())
        {
            return;
        }
        const int nProgress = m_pReader->chapterProgress();
        const QSignalBlocker oBlocker(m_pReaderProgress);
        m_pReaderProgress->setValue(nProgress);
        m_pReaderProgress->setEnabled(m_nChapter >= 0 && m_pRead->isEnabled() &&
                                      !m_bLoadingBusy && m_pReader->canSeekProgress());
        m_pReaderProgressPercent->setText(QString("%1%").arg(nProgress / 10));
    }
    void ClientWindow::turnReaderPage(int nDirection)
    {
        if (m_bLoadingBusy)
            return;
        if (!m_bUnlocked || m_pPages->currentIndex() != 3 || m_nChapter < 0 || m_bRestoring ||
            !m_pRead->isEnabled())
            return;
        if (m_pReader->turnPage(nDirection))
            return;
        int nNext = m_nChapter + (nDirection < 0 ? -1 : 1);
        if (nNext >= 0 && nNext < m_vFlat.size())
        {
            m_nRestoreOffset = nDirection < 0 ? std::numeric_limits<int>::max() : 0;
            openChapter(nNext);
        }
        else
            message(nDirection < 0 ? "已到本书开头。" : "已到本书末尾。");
    }
    void ClientWindow::updateVolumePaging()
    {
        setAndroidReadingActive(m_bUnlocked && m_pPages->currentIndex() == 3 &&
                                QGuiApplication::applicationState() == Qt::ApplicationActive,
                                m_bKeepAwake);
        setAndroidVolumePaging(m_bVolumePaging && m_bUnlocked && !m_bLoadingBusy &&
                               m_pPages->currentIndex() == 3 &&
                               QGuiApplication::applicationState() == Qt::ApplicationActive);
    }
}
