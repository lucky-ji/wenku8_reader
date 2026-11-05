// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <QVector>
namespace Wenku8::Reader
{
    struct LicenseEntry
    {
        QString m_strTitle, m_strLicense, m_strResource;
    };
    QString applicationName();
    QString applicationVersion();
    QString versionDescription();
    QString releaseNotes();
    QString licenseOverview();
    QString licenseText(const QString& strResource);
    QVector<LicenseEntry> applicationLicenses();
} // namespace Wenku8::Reader
