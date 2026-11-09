// SPDX-License-Identifier: GPL-2.0-only
#include "ExportDestination.h"
#include "Wenku8Sdk.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
namespace Wenku8::Reader
{
    namespace
    {
        QString resolvedPath(const QString& strPath)
        {
            const QFileInfo oInfo(strPath);
            const QString strCanonical = oInfo.canonicalFilePath();
            return QDir::cleanPath(strCanonical.isEmpty() ? oInfo.absoluteFilePath() : strCanonical);
        }
        bool within(const QString& strPath, const QString& strRoot)
        {
#ifdef Q_OS_WIN
            constexpr auto eCase = Qt::CaseInsensitive;
#else
            constexpr auto eCase = Qt::CaseSensitive;
#endif
            return strPath.compare(strRoot, eCase) == 0 ||
                   strPath.startsWith(strRoot + '/', eCase);
        }
        void checkContinue(const std::function<bool()>& fnContinue)
        {
            if (fnContinue && !fnContinue())
            {
                throw Error(ErrorCode::Cancelled, "已取消下载到本地。");
            }
        }
        QString safeDirectoryName(QString strName)
        {
            strName.replace(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]"), "_");
            strName = strName.trimmed().left(100);
            while (strName.endsWith('.') || strName.endsWith(' '))
            {
                strName.chop(1);
            }
            if (strName.isEmpty() || strName == "." || strName == "..")
            {
                strName = "轻小说";
            }
            const QString strStem = strName.section('.', 0, 0).toUpper();
            if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$").match(strStem).hasMatch())
            {
                strName.prepend("小说-");
            }
            return strName;
        }
        void copyDirectory(const QString& strSource, const QString& strDestination,
                           const QString& strSourceRoot, const std::function<bool()>& fnContinue)
        {
            const auto vEntries = QDir(strSource).entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
            for (const auto& oEntry : vEntries)
            {
                checkContinue(fnContinue);
                if (oEntry.isSymLink() || !within(oEntry.canonicalFilePath(), strSourceRoot))
                {
                    throw Error(ErrorCode::Io, "导出内容包含不支持的链接，未保存到本地。");
                }
                const QString strTarget = QDir(strDestination).filePath(oEntry.fileName());
                if (oEntry.isDir())
                {
                    if (!QDir().mkdir(strTarget))
                    {
                        throw Error(ErrorCode::Io, "无法创建小说目录，请检查保存位置的写入权限。");
                    }
                    copyDirectory(oEntry.absoluteFilePath(), strTarget, strSourceRoot, fnContinue);
                    continue;
                }
                if (!oEntry.isFile())
                {
                    throw Error(ErrorCode::Io, "导出内容包含不支持的文件类型。");
                }
                QFile oInput(oEntry.absoluteFilePath());
                QSaveFile oOutput(strTarget);
                oOutput.setDirectWriteFallback(false);
                if (!oInput.open(QIODevice::ReadOnly) || !oOutput.open(QIODevice::WriteOnly))
                {
                    throw Error(ErrorCode::Io, "无法写入小说文件，请检查磁盘空间和保存目录权限。");
                }
                while (!oInput.atEnd())
                {
                    checkContinue(fnContinue);
                    const QByteArray baChunk = oInput.read(64 * 1024);
                    if (baChunk.isEmpty() && oInput.error() != QFile::NoError)
                    {
                        throw Error(ErrorCode::Io, "无法读取待保存的小说文件。");
                    }
                    if (oOutput.write(baChunk) != baChunk.size())
                    {
                        throw Error(ErrorCode::Io, "小说文件写入失败，请检查磁盘剩余空间。");
                    }
                }
                checkContinue(fnContinue);
                if (!oOutput.commit())
                {
                    throw Error(ErrorCode::Io, "小说文件保存失败，请重新选择保存目录。");
                }
            }
        }
    }
    QString privateDownloadDirectory()
    {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("books");
    }
    QString exportDirectoryLabel(const QString& strLocation)
    {
        return strLocation.isEmpty() ? QString() : QDir::toNativeSeparators(resolvedPath(strLocation));
    }
    bool exportDirectoryAvailable(const QString& strLocation)
    {
        if (strLocation.isEmpty())
        {
            return false;
        }
        const QFileInfo oDirectory(strLocation);
        return oDirectory.isDir() && oDirectory.isWritable() &&
               !within(resolvedPath(strLocation), resolvedPath(privateDownloadDirectory()));
    }
    void chooseExportDirectory(QWidget* pParent,
                               std::function<void(const ExportDirectory&)> fnResult)
    {
        if (!fnResult)
        {
            return;
        }
        const QString strDirectory = QFileDialog::getExistingDirectory(
            pParent, "选择下载到本地的保存目录",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            QFileDialog::ShowDirsOnly);
        if (strDirectory.isEmpty())
        {
            fnResult({});
            return;
        }
        fnResult({resolvedPath(strDirectory), exportDirectoryLabel(strDirectory), {}});
    }
    QString publishExportDirectory(const QString& strStagedBookDirectory,
                                   const QString& strDestination,
                                   std::function<bool()> fnContinue)
    {
        checkContinue(fnContinue);
        const QFileInfo oSource(strStagedBookDirectory);
        if (!oSource.isDir() || oSource.isSymLink())
        {
            throw Error(ErrorCode::Io, "待保存的小说目录不可用，请重新下载。");
        }
        const QString strSource = resolvedPath(strStagedBookDirectory);
        const QString strRoot = resolvedPath(strDestination);
        if (!exportDirectoryAvailable(strDestination) || within(strRoot, strSource))
        {
            throw Error(ErrorCode::Io, "保存目录不可用，请选择缓存目录之外且可写入的目录。");
        }
        const QString strName = safeDirectoryName(oSource.fileName());
        QString strCreated;
        QDir oRoot(strRoot);
        for (int nIndex = 0; nIndex < 10000; ++nIndex)
        {
            checkContinue(fnContinue);
            const QString strCandidate = strName + (nIndex ? QString(" (%1)").arg(nIndex) : QString());
            if (oRoot.mkdir(strCandidate))
            {
                strCreated = oRoot.filePath(strCandidate);
                break;
            }
            if (!oRoot.exists(strCandidate))
            {
                throw Error(ErrorCode::Io, "无法创建本地小说目录，请检查写入权限和磁盘空间。");
            }
        }
        if (strCreated.isEmpty())
        {
            throw Error(ErrorCode::Io, "保存目录中重名文件过多，请重新选择目录。");
        }
        try
        {
            copyDirectory(strSource, strCreated, strSource, fnContinue);
            checkContinue(fnContinue);
        }
        catch (...)
        {
            // Only the uniquely created directory belongs to this operation.
            QDir(strCreated).removeRecursively();
            throw;
        }
        return QDir::toNativeSeparators(strCreated);
    }
} // namespace Wenku8::Reader
