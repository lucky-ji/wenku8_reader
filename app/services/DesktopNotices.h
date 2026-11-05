// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <QVector>
namespace Wenku8::Reader
{
    struct DesktopNotice
    {
        QString m_strTitle, m_strFile;
    };
    QString desktopNoticeDirectory();
    QVector<DesktopNotice> desktopNotices(const QString& strDirectory);
    QString desktopNoticeText(const QString& strDirectory, const QString& strFile);
} // namespace Wenku8::Reader
