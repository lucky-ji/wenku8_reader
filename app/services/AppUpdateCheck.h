// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QFutureWatcher>
#include <QString>
#include <QUrl>

namespace Wenku8::Reader
{
    class AppUpdateCheck final : public QObject
    {
        Q_OBJECT

      public:
        struct Result
        {
            enum class Status
            {
                UpToDate,
                UpdateAvailable,
                Failed
            };

            Status eStatus = Status::Failed;
            QString strVersion;
            QUrl oReleaseUrl;
        };

        explicit AppUpdateCheck(QObject* pParent = nullptr);
        void check();

      signals:
        void finished(const Result& oResult);

      private:
        QFutureWatcher<Result> m_oWatcher;
    };
} // namespace Wenku8::Reader
