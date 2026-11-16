// SPDX-License-Identifier: GPL-2.0-only
#include "AndroidKeys.h"
#include <QAndroidJniObject>
#include <QAndroidJniEnvironment>
#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <QMetaObject>
#include <utility>
#include <jni.h>
namespace Wenku8::Reader
{
    namespace
    {
        std::function<void()> fnSafeInsetsChanged;
    }

    void setAndroidReadingActive(bool bActive, bool bKeepAwake)
    {
        static int nPreviousActive = -1;
        static int nPreviousKeepAwake = -1;
        if (nPreviousActive == int(bActive) && nPreviousKeepAwake == int(bKeepAwake))
        {
            return;
        }
        if (QAndroidJniObject::isClassAvailable("io/github/lucky_ji/wenku8_reader/ReaderActivity"))
        {
            QAndroidJniObject::callStaticMethod<void>(
                "io/github/lucky_ji/wenku8_reader/ReaderActivity", "setReadingPreferences", "(ZZ)V",
                jboolean(bActive), jboolean(bKeepAwake));
            nPreviousActive = int(bActive);
            nPreviousKeepAwake = int(bKeepAwake);
        }
    }

    QMargins getAndroidSafeInsets()
    {
        if (!QAndroidJniObject::isClassAvailable("io/github/lucky_ji/wenku8_reader/ReaderActivity"))
        {
            return {};
        }
        const auto oInsets = QAndroidJniObject::callStaticObjectMethod(
            "io/github/lucky_ji/wenku8_reader/ReaderActivity", "getReadingSafeInsets", "()[I");
        QAndroidJniEnvironment oEnvironment;
        if (oEnvironment->ExceptionCheck())
        {
            oEnvironment->ExceptionClear();
            return {};
        }
        if (!oInsets.isValid() || oEnvironment->GetArrayLength(oInsets.object<jintArray>()) != 4)
        {
            return {};
        }
        jint vInsets[4] = {};
        oEnvironment->GetIntArrayRegion(oInsets.object<jintArray>(), 0, 4, vInsets);
        if (oEnvironment->ExceptionCheck())
        {
            oEnvironment->ExceptionClear();
            return {};
        }
        return {qMax(0, int(vInsets[0])), qMax(0, int(vInsets[1])),
                qMax(0, int(vInsets[2])), qMax(0, int(vInsets[3]))};
    }

    void setAndroidSafeInsetsChangedHandler(std::function<void()> fnChanged)
    {
        fnSafeInsetsChanged = std::move(fnChanged);
    }

    void notifyAndroidSafeInsetsChanged()
    {
        if (!qApp)
        {
            return;
        }
        QMetaObject::invokeMethod(qApp,
            []
            {
                if (fnSafeInsetsChanged)
                {
                    fnSafeInsetsChanged();
                }
            }, Qt::QueuedConnection);
    }

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
Java_io_github_lucky_1ji_wenku8_1reader_ReaderActivity_nativeReadingInsetsChanged(JNIEnv*, jclass)
{
    Wenku8::Reader::notifyAndroidSafeInsetsChanged();
}

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
