// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QByteArray>
namespace Wenku8::Reader
{
    // QtSvg is for trusted application icons only. Novel/cover bytes must not
    // acquire SVG's external-resource behavior when that plugin becomes available.
    inline QByteArray rasterFormat(const QByteArray& baBytes)
    {
        if (baBytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
            return "png";
        if (baBytes.startsWith(QByteArray::fromHex("ffd8ff")))
            return "jpeg";
        if (baBytes.startsWith("GIF87a") || baBytes.startsWith("GIF89a"))
            return "gif";
        if (baBytes.startsWith("RIFF") && baBytes.mid(8, 4) == "WEBP")
            return "webp";
        if (baBytes.startsWith("BM"))
            return "bmp";
        if (baBytes.startsWith(QByteArray::fromHex("49492a00")) ||
            baBytes.startsWith(QByteArray::fromHex("4d4d002a")))
            return "tiff";
        if (baBytes.startsWith(QByteArray::fromHex("00000100")))
            return "ico";
        return {};
    }
} // namespace Wenku8::Reader
