// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Wenku8Sdk.h"
namespace Wenku8::Detail
{
    void downloadTextPackage(int, const std::filesystem::path&, CachePolicy, HttpTransport&,
                             const TransferOptions&, FileCache*, std::chrono::seconds);
}
