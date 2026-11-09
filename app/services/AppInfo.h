// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <QUrl>
#include <QVector>
namespace Wenku8::Reader
{
    struct LicenseEntry
    {
        QString m_strTitle, m_strLicense, m_strResource;
    };
    QString applicationName();
    QString applicationVersion();
    QUrl applicationRepositoryUrl();
    QUrl applicationFeedbackUrl();
    bool isApplicationLink(const QUrl& oUrl);
    QString versionDescription();
    QString releaseNotes();
    QString licenseOverview();
    QString licenseText(const QString& strResource);
    QVector<LicenseEntry> applicationLicenses();
} // namespace Wenku8::Reader
