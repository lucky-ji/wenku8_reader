// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
#include <QJsonObject>
#include <QUrl>
namespace Wenku8::Reader::MobileBrowserProtocol
{
    bool sameOrigin(const QUrl&, const QUrl&);
    void validateOrigin(const QUrl&);
    QJsonObject requestConfig(const QUrl&, const HttpRequest&, const TransferOptions&);
    HttpResponse response(const QUrl&, const QByteArray&, std::size_t nLimit);
} // namespace Wenku8::Reader::MobileBrowserProtocol
