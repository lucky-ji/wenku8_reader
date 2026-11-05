// SPDX-License-Identifier: GPL-2.0-only
#include "AndroidKeys.h"
#include <QAndroidJniObject>
#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <QMetaObject>
#include <jni.h>
namespace Wenku8::Reader
{
    void setAndroidVolumePaging(bool bEnabled)
    {
        static int nPrevious = -1;
        if (nPrevious == int(bEnabled))
            return;
        nPrevious = int(bEnabled);
        if (QAndroidJniObject::isClassAvailable("io/github/lucky_ji/wenku8_reader/ReaderActivity"))
            QAndroidJniObject::callStaticMethod<void>(
                "io/github/lucky_ji/wenku8_reader/ReaderActivity", "setVolumePagingEnabled", "(Z)V",
                jboolean(bEnabled));
    }
} // namespace Wenku8::Reader
extern "C" JNIEXPORT void JNICALL
Java_io_github_lucky_1ji_wenku8_1reader_ReaderActivity_nativeVolumePage(JNIEnv*, jclass,
                                                                        jint nDirection)
{
    if (!qApp)
        return;
    QMetaObject::invokeMethod(
        qApp,
        [nDirection]
        {
            if (QGuiApplication::applicationState() != Qt::ApplicationActive)
                return;
            auto pWindow = QApplication::activeWindow();
            if (!pWindow)
                for (auto pCandidate : QApplication::topLevelWidgets())
                    if (pCandidate->isVisible() && pCandidate->objectName() == "clientWindow")
                    {
                        pWindow = pCandidate;
                        break;
                    }
            if (pWindow)
            {
                QKeyEvent oEvent(QEvent::KeyPress,
                                 nDirection < 0 ? Qt::Key_VolumeUp : Qt::Key_VolumeDown,
                                 Qt::NoModifier);
                QApplication::sendEvent(pWindow, &oEvent);
            }
        },
        Qt::QueuedConnection);
}
