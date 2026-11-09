// SPDX-License-Identifier: GPL-2.0-only
#include "ClientWindow.h"
#include "AppInfo.h"
#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QStyleFactory>
#include <memory>
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#endif

using namespace Wenku8::Reader;

int main(int nArgumentCount, char** ppArguments)
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication oApplication(nArgumentCount, ppArguments);
    Q_INIT_RESOURCE(icons);
    oApplication.setWindowIcon(QIcon(":/wenku8/icons/app-icon.png"));
    oApplication.setStyle(QStyleFactory::create("Fusion"));
#ifdef Q_OS_WIN
    oApplication.setFont(QFont("Microsoft YaHei UI", 10));
#endif
    oApplication.setOrganizationName("Wenku8Cpp");
    oApplication.setApplicationName("Wenku8Reader");
    oApplication.setApplicationVersion(applicationVersion());
    oApplication.setApplicationDisplayName(applicationName());
    QCommandLineParser oArguments;
    oArguments.setApplicationDescription(applicationName());
    oArguments.addHelpOption();
    oArguments.addVersionOption();
    oArguments.addOption({"origin", "文库来源", "url", "https://www.wenku8.net"});
    oArguments.addOption({"novel", "登录后打开的小说 ID", "id"});
    oArguments.process(oApplication);
    const QString strApplicationDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(strApplicationDirectory);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    const QString strInstanceName =
        "wenku8-reader-" +
        QString::fromLatin1(
            QCryptographicHash::hash(strApplicationDirectory.toUtf8(), QCryptographicHash::Sha256)
                .toHex()
                .left(24));
    QLockFile oInstanceLock(QDir(strApplicationDirectory).filePath("client-instance.lock"));
    oInstanceLock.setStaleLockTime(0);
    if (!oInstanceLock.tryLock(0))
    {
        QLocalSocket oSocket;
        oSocket.connectToServer(strInstanceName);
        if (oSocket.waitForConnected(1000))
        {
            oSocket.write("activate");
            oSocket.waitForBytesWritten(1000);
            return 0;
        }
        QMessageBox::information(nullptr, applicationName(), "客户端已在运行，请先关闭旧窗口。");
        return 0;
    }
    QLocalServer::removeServer(strInstanceName);
    QLocalServer oInstanceServer;
    oInstanceServer.listen(strInstanceName);
#endif
    const QString strSessionDirectory =
        QDir(strApplicationDirectory)
            .filePath("sessions/" + QString::fromLatin1(QCryptographicHash::hash(
                                                            oArguments.value("origin").toUtf8(),
                                                            QCryptographicHash::Sha256)
                                                            .toHex()));
    ClientWindow oWindow(QString(), oArguments.value("origin"),
                         strSessionDirectory);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QObject::connect(&oInstanceServer, &QLocalServer::newConnection, &oWindow,
                     [&]()
                     {
                         while (auto* pSocket = oInstanceServer.nextPendingConnection())
                         {
                             pSocket->close();
                             pSocket->deleteLater();
                         }
                         if (oWindow.isMinimized())
                         {
                             oWindow.showNormal();
                         }
                         oWindow.raise();
                         oWindow.activateWindow();
                         if (auto* pModal = QApplication::activeModalWidget())
                         {
                             pModal->raise();
                             pModal->activateWindow();
                         }
                     });
#endif
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    oWindow.showMaximized();
#else
    oWindow.show();
#endif
    oWindow.start(oArguments.value("novel").toInt());
    return oApplication.exec();
}

