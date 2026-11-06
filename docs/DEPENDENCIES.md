# 依赖版本与源码来源

工程与 SDK 源码从 [主仓库](https://github.com/lucky-ji/wenku8_reader) 获取：

```sh
git clone https://github.com/lucky-ji/wenku8_reader.git
cd wenku8_reader
```

默认使用 `main`。对应正式版本的代码可通过 `git switch --detach v1.0.1` 定位；最新环境说明见主仓库的 [构建指南](BUILDING.md)。Release 手动附件用于下载安装编译好的程序，不是自编译的工程下载入口。

## 仓库中已有的源码

| 组件 | 版本 | 位置 |
| --- | --- | --- |
| curl | 8.0.1 | `third_party/curl-8.0.1.zip`，保持原始归档 |
| libxml2 | 2.15.4 | `third_party/libxml2-2.15.4.tar.xz`，保持原始归档 |
| TinyXML-2 | 10.0.0 | `third_party/tinyxml2/` |
| LLVM libc++ filesystem | 9.0.1 | `third_party/libcxx-filesystem/`，用于 NDK r21 |

## 另行获取的匹配依赖

Qt、VS、NDK 等开发环境不包含在主仓库中。编译客户端先安装匹配的 Qt 5 kit；下表提供依赖的完整原始源码入口，供构建依赖、核对版本及再分发使用。下载源码不等于已经安装相应开发环境。

| 组件 | 版本和用途 | 固定版本源码 |
| --- | --- | --- |
| Qt | 5.15.2；官方 Windows、Android、macOS Intel 与 Linux x86_64 kit | [完整源码 tar.xz](https://download.qt.io/archive/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz)，包含 WebEngine/Chromium 和各模块原始声明 |
| Mbed TLS | 3.6.5；Android 网络依赖 | [完整发布源码 tar.bz2](https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2)，需包含 framework 等发布子目录 |
| GNU libiconv | 1.17；Android 编码转换 | [原始 tar.gz](https://ftp.gnu.org/gnu/libiconv/libiconv-1.17.tar.gz) |
| OpenSSL | 1.1.1t；部分 Windows Qt 组件依赖 | [官方历史源码](https://www.openssl.org/source/old/1.1.1/openssl-1.1.1t.tar.gz) · [对应 GitHub 发布](https://github.com/openssl/openssl/releases/tag/OpenSSL_1_1_1t) |
| zlib | 1.2.13；部分 Qt 组件依赖 | [原始 tar.gz](https://zlib.net/fossils/zlib-1.2.13.tar.gz) |
| ICU | 56.1；官方 Linux Qt kit 的 Unicode 库 | [原始 tgz](https://github.com/unicode-org/icu/releases/download/release-56-1/icu4c-56_1-src.tgz) |

原始源码 SHA-256：

```text
qt-everywhere-src-5.15.2.tar.xz  3a530d1b243b5dec00bc54937455471aaa3e56849d2593edb8ded07228202240
mbedtls-3.6.5.tar.bz2           4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
libiconv-1.17.tar.gz            8f74213b56238c85a50a5329f77e06198771e70dd9a739779f4c02f65d971313
openssl-1.1.1t.tar.gz           8dee9b24bdb1dcbf0c3d1e9b02fb8f6bf22165e807f45adeb7c9677536859d3b
zlib-1.2.13.tar.gz              b3a24de97a8fdbc835b9833169501030b8977031bcb54b3b3ac13740f846ab30
icu4c-56_1-src.tgz              3a64e9105c734dcf631c0b3ed60404531bce6c0f5a64bfe1a6402a4cc2314816
```

Windows 可使用 `Get-FileHash -Algorithm SHA256 <文件>`，Linux/macOS 可使用 `sha256sum <文件>` 或 `shasum -a 256 <文件>` 校验。Android 四 ABI 的依赖构建步骤见 [ANDROID_BUILD.md](ANDROID_BUILD.md)。curl 与 libxml2 的原始归档摘要另列在 [第三方说明](../THIRD_PARTY_NOTICES.md)。

## 系统提供的组件

Ubuntu 24.04 ARM64 使用系统 Qt 5.15.13、Qt WebEngine 5.15.16 与 Mbed TLS 2.28.8；Linux x86_64 的 TLS 后端也来自发行版 Mbed TLS。精确包名和版本在各运行包的 `BUILD-METADATA.json` 中记录，系统前置库见 `RUNTIME-DEPENDENCIES.txt`。可以通过 Ubuntu 的 source package 仓库取得对应版本源码，例如先启用匹配 Ubuntu 发行版的 `deb-src`，再使用 `apt-get source <源码包名>=<源码包版本>`。

这些 OS 库没有随 Linux 程序包复制。macOS 的 SecureTransport 和系统 Iconv 由操作系统提供，不是本仓库附带的第三方二进制。

## 许可证与分发

原始许可证与声明见 [第三方说明](../THIRD_PARTY_NOTICES.md) 和 `licenses/`。源码获取入口调整不改变 GPL 及其他许可证规定的对应源码义务；分发者应保持与所分发二进制匹配的源码、构建配置及原始声明可获取。工程源码、第三方源码和编译环境分别按上述入口取得，不需要下载 Release 上的源码整合包。
