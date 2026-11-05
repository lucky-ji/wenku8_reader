// SPDX-License-Identifier: GPL-2.0-only
#include "DesktopNotices.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>
#include <algorithm>
namespace Wenku8::Reader
{
    namespace
    {
        constexpr qint64 nTextLimit = 4 * 1024 * 1024;
        QString canonicalRoot(const QString& strDirectory)
        {
            if (strDirectory.isEmpty())
                return {};
            const QFileInfo oRoot(strDirectory);
            return oRoot.isDir() ? oRoot.canonicalFilePath() : QString();
        }
        QString safeFile(const QString& strRoot, const QString& strName)
        {
            // INDEX contains opaque basenames, never paths, drive names or NTFS streams.
            if (strRoot.isEmpty() || strName.isEmpty() || strName == "." || strName == ".." ||
                strName.contains('/') || strName.contains('\\') || strName.contains(':') ||
                strName.contains(QChar(0)))
                return {};
            for (const auto oCharacter : strName)
                if (oCharacter.unicode() < 32 || oCharacter.unicode() == 127)
                    return {};
            const QFileInfo oFile(QDir(strRoot).filePath(strName));
            if (!oFile.isFile())
                return {};
            const auto strCanonical = oFile.canonicalFilePath();
            if (strCanonical.isEmpty())
                return {};
#ifdef Q_OS_WIN
            const auto eSensitivity = Qt::CaseInsensitive;
#else
            const auto eSensitivity = Qt::CaseSensitive;
#endif
            if (QFileInfo(strCanonical).dir().canonicalPath().compare(strRoot, eSensitivity) != 0)
                return {};
            return strCanonical;
        }
        bool readText(const QString& strPath, QString& strText)
        {
            QFile oFile(strPath);
            if (strPath.isEmpty() || !oFile.open(QIODevice::ReadOnly) || oFile.size() > nTextLimit)
                return false;
            const auto baBytes = oFile.read(nTextLimit + 1);
            if (baBytes.size() > nTextLimit || baBytes.contains('\0'))
                return false;
            QTextCodec::ConverterState oState;
            strText = QTextCodec::codecForName("UTF-8")->toUnicode(baBytes.constData(),
                                                                   baBytes.size(), &oState);
            if (oState.invalidChars)
                return false;
            if (strText.startsWith(QChar(0xfeff)))
                strText.remove(0, 1);
            for (const auto oCharacter : strText)
            {
                const auto nValue = oCharacter.unicode();
                if ((nValue < 32 && nValue != '\t' && nValue != '\r' && nValue != '\n') ||
                    nValue == 127)
                    return false;
            }
            return true;
        }
        bool legalSource(const QString& strSource)
        {
            if (strSource.isEmpty() || strSource.contains(QChar(0)) ||
                QDir::isAbsolutePath(strSource) || strSource.contains('\\') ||
                strSource.contains(':'))
                return false;
            const auto vParts = strSource.split('/');
            for (const auto& strPart : vParts)
                if (strPart.isEmpty() || strPart == "." || strPart == "..")
                    return false;
            for (const auto oCharacter : strSource)
                if (oCharacter.unicode() < 32 || oCharacter.unicode() == 127)
                    return false;
            const auto strBase = vParts.last().toLower();
            if (strBase == "qt_attribution.json")
                return true;
            // Match an actual legal filename, not Qt's licensewizard/licensepage code.
            static const QRegularExpression oLegalName(
                "^(licen[sc]es?|copying|copyrights?|notices?)(?:$|[._-])",
                QRegularExpression::CaseInsensitiveOption);
            if (!oLegalName.match(strBase).hasMatch())
                return false;
            static const QSet<QString> vExcluded = {
                "png",  "jpg",     "jpeg", "gif",   "webp",  "bmp", "ico", "pdf",   "zip",
                "7z",   "gz",      "bz2",  "xz",    "py",    "cpp", "cc",  "c",     "h",
                "hpp",  "js",      "ts",   "sh",    "bat",   "cmd", "ps1", "exe",   "dll",
                "so",   "a",       "lib",  "obj",   "class", "jar", "bin", "pro",   "qrc",
                "qdoc", "pri",     "prf",  "cmake", "ui",    "qml", "qbs", "ninja", "make",
                "mak",  "vcxproj", "sln",  "cs",    "css",   "ini"};
            return !vExcluded.contains(QFileInfo(strBase).suffix());
        }
    } // namespace
    QString desktopNoticeDirectory()
    {
        const auto strApp = QCoreApplication::applicationDirPath();
        for (const auto& strCandidate :
             {QDir(strApp).filePath("licenses/qt-components"),
              QDir(strApp).filePath("../share/wenku8/licenses/qt-components")})
        {
            const auto strRoot = canonicalRoot(strCandidate);
            if (!safeFile(strRoot, "INDEX.txt").isEmpty())
                return strRoot;
        }
        return {};
    }
    QVector<DesktopNotice> desktopNotices(const QString& strDirectory)
    {
        const auto strRoot = canonicalRoot(strDirectory);
        QString strIndex;
        if (!readText(safeFile(strRoot, "INDEX.txt"), strIndex))
            return {};
        QVector<DesktopNotice> vResult;
        QSet<QString> vSeen;
        for (const auto& strRaw : strIndex.split('\n'))
        {
            const auto strLine = strRaw.endsWith('\r') ? strRaw.left(strRaw.size() - 1) : strRaw;
            if (strLine.size() > 4096 || strLine.count('\t') != 1)
                continue;
            const auto nSeparator = strLine.indexOf('\t');
            const auto strSource = strLine.left(nSeparator), strName = strLine.mid(nSeparator + 1);
            if (!legalSource(strSource))
                continue;
            const auto strPath = safeFile(strRoot, strName);
            if (strPath.isEmpty() || QFileInfo(strPath).size() > nTextLimit)
                continue;
#ifdef Q_OS_WIN
            const auto strKey = strPath.toCaseFolded();
#else
            const auto strKey = strPath;
#endif
            if (vSeen.contains(strKey))
                continue;
            vSeen.insert(strKey);
            vResult.append({strSource, strName});
            if (vResult.size() >= 16384)
                break;
        }
        std::sort(vResult.begin(), vResult.end(),
                  [](const DesktopNotice& oA, const DesktopNotice& oB)
                  {
                      return oA.m_strTitle.compare(oB.m_strTitle, Qt::CaseInsensitive) < 0;
                  });
        return vResult;
    }
    QString desktopNoticeText(const QString& strDirectory, const QString& strFile)
    {
        QString strText;
        const auto strPath = safeFile(canonicalRoot(strDirectory), strFile);
        if (strPath.isEmpty())
            return "暂时无法读取此许可，请查看发布包中的原始许可证文件。";
        if (!readText(strPath, strText))
            return "此文件不是可显示的许可文本，或文件过大。请查看发布包中的原始许可证文件。";
        return strText;
    }
} // namespace Wenku8::Reader
