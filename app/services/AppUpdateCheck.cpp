// SPDX-License-Identifier: GPL-2.0-only
#include "AppUpdateCheck.h"
#include "AppInfo.h"
#include "ClientNetwork.h"
#include <QRegularExpression>
#include <QVersionNumber>
#include <QtConcurrent/QtConcurrentRun>

namespace Wenku8::Reader
{
    namespace
    {
        QVersionNumber parseVersion(const QString& strVersion)
        {
            static const QRegularExpression oPattern(
                QStringLiteral(R"(\Av?((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))\z)"));
            const auto oMatch = oPattern.match(strVersion);
            if (!oMatch.hasMatch())
            {
                return {};
            }
            const auto strNumber = oMatch.captured(1);
            int nParsed = 0;
            const auto oVersion = QVersionNumber::fromString(strNumber, &nParsed);
            return nParsed == strNumber.size() ? oVersion : QVersionNumber();
        }

        AppUpdateCheck::Result checkLatestRelease()
        {
            const auto strRepository = applicationRepositoryUrl().toString();
            const auto oLatestUrl = QUrl(strRepository + QStringLiteral("/releases/latest"));
            const auto oCurrent = parseVersion(applicationVersion());
            const auto strTagPrefix = strRepository + QStringLiteral("/releases/tag/");

            HttpRequest oRequest;
            oRequest.m_strUrl = oLatestUrl.toString().toStdString();
            oRequest.m_vHeaders = {{"Accept", "text/html"}, {"User-Agent", "Wenku8Reader"}};
            TransferOptions oLimits;
            // GitHub's public latest-release link redirects to the stable tag without an API quota.
            oLimits.m_bFollowRedirects = false;
            oLimits.m_oConnectTimeout = std::chrono::seconds(5);
            oLimits.m_oTimeout = std::chrono::seconds(10);
            oLimits.m_nMaxResponseBytes = 32 * 1024;
            try
            {
                const auto oResponse = makeClientUpdateTransport()->perform(oRequest, oLimits);
                switch (oResponse.m_nStatus)
                {
                case 301:
                case 302:
                case 303:
                case 307:
                case 308:
                    break;
                default:
                    return {};
                }
                for (const auto& oHeader : oResponse.m_vHeaders)
                {
                    if (QByteArray::fromStdString(oHeader.first).compare("Location", Qt::CaseInsensitive) != 0)
                    {
                        continue;
                    }
                    const auto oRelease = oLatestUrl.resolved(
                        QUrl::fromEncoded(QByteArray::fromStdString(oHeader.second), QUrl::StrictMode));
                    const auto strRelease = oRelease.toString(QUrl::FullyEncoded);
                    if (!oRelease.isValid() || !strRelease.startsWith(strTagPrefix))
                    {
                        return {};
                    }
                    const auto oNewest = parseVersion(strRelease.mid(strTagPrefix.size()));
                    if (oCurrent.isNull() || oNewest.isNull())
                    {
                        return {};
                    }
                    using Status = AppUpdateCheck::Result::Status;
                    if (oNewest <= oCurrent)
                    {
                        return {Status::UpToDate, oNewest.toString(), {}};
                    }
                    return {Status::UpdateAvailable, oNewest.toString(), oRelease};
                }
            }
            catch (const std::exception&)
            {
                return {};
            }
            return {};
        }
    } // namespace

    AppUpdateCheck::AppUpdateCheck(QObject* pParent) : QObject(pParent)
    {
        connect(&m_oWatcher, &QFutureWatcher<Result>::finished, this, [this]
        {
            emit finished(m_oWatcher.result());
        });
    }

    void AppUpdateCheck::check()
    {
        if (!m_oWatcher.isRunning())
        {
            m_oWatcher.setFuture(QtConcurrent::run(checkLatestRelease));
        }
    }
} // namespace Wenku8::Reader
