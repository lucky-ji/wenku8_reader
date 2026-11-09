// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <functional>
class QWidget;
namespace Wenku8::Reader
{
    struct ExportDirectory
    {
        QString m_strLocation, m_strLabel, m_strError;
    };
    // Location is an OS path on desktop and a persisted document-tree URI on Android.
    void chooseExportDirectory(QWidget* pParent,
                               std::function<void(const ExportDirectory&)> fnResult);
    QString exportDirectoryLabel(const QString& strLocation);
    bool exportDirectoryAvailable(const QString& strLocation);
    // Called on a worker. Publishes one new, uniquely named book directory and never
    // overwrites an existing directory; fnContinue may cancel the copy.
    QString publishExportDirectory(const QString& strStagedBookDirectory,
                                   const QString& strDestination,
                                   std::function<bool()> fnContinue = {});
    QString privateDownloadDirectory();
} // namespace Wenku8::Reader
