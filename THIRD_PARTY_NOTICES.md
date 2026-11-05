# 第三方来源与许可证

轻小说文库及其 SDK 按 GPL-2.0-only 提供，完整正文见 [LICENSE](LICENSE)。下列组件各自的版权、许可和源文件声明继续有效，不能以本项目许可证替代。第三方源码不参与自有代码的命名或格式整理。

## 分发组件

| 组件 | 版本与用途 | 许可证与原始文本 |
| --- | --- | --- |
| [curl](https://curl.se/) | 8.0.1；SDK HTTP 传输 | curl license；[完整文本](licenses/curl-8.0.1.txt)，源码归档中的 COPYING |
| [TinyXML-2](https://github.com/leethomason/tinyxml2/tree/10.0.0) | 10.0.0；XML 与存储清单 | zlib license；[原始 LICENSE](third_party/tinyxml2/LICENSE.txt) 与源文件头 |
| [libxml2](https://gitlab.gnome.org/GNOME/libxml2) | 2.15.4；HTML/XPath 解析 | MIT 及部分源文件补充声明；[完整文本](licenses/libxml2-2.15.4.txt) |
| [Qt](https://www.qt.io/) | 5.15.2；Core、Gui、Widgets、Svg、Concurrent、平台与图片插件 | 本项目采用相应模块提供的 GPL-2.0 许可选项；[GPL 正文](licenses/Qt-5.15.2-GPL-2.0.txt)。Qt 内置组件声明随对应源码和发行包保留 |
| [Qt WebEngine / Chromium](https://doc.qt.io/archives/qt-5.15/qtwebengine-licensing.html) | 桌面登录与会话；包括 WebChannel 等传递依赖 | Qt 模块许可及 Chromium 多种第三方许可证；[保留声明](licenses/Qt-5.15.2-Chromium.txt)，完整组件归属随实际 Qt 源码和发行包提供 |
| [GNU libiconv / libcharset](https://www.gnu.org/software/libiconv/) | 1.17；Android 编码转换 | LGPL-2.1-or-later；[完整文本](licenses/libiconv-1.17.txt) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.5) | 3.6.5；Android 原生网络依赖 | 采用 GPL-2.0-or-later 许可选项；[完整文本](licenses/mbedtls-3.6.5.txt)，内置第三方声明以原源码为准 |
| [LLVM libc++ filesystem](https://github.com/llvm/llvm-project/tree/llvmorg-9.0.1/libcxx) | 9.0.1；补足 NDK r21 的 filesystem 实现 | Apache-2.0 WITH LLVM-exception，含 GPL 兼容例外；[完整文本](third_party/libcxx-filesystem/LICENSE.TXT) |
| [OpenSSL](https://www.openssl.org/) | 1.1.1t；部分桌面 Qt 运行组件依赖 | OpenSSL / SSLeay 双许可证；[完整文本](licenses/OpenSSL-1.1.1t.txt) |
| [zlib](https://zlib.net/) | 1.2.13；部分 Qt 运行组件依赖 | zlib license；[完整文本](licenses/zlib-1.2.13.txt) |
| [ICU](https://github.com/unicode-org/icu/releases/tag/release-56-1) | 56.1；官方 Linux Qt kit 的 Unicode 运行依赖 | ICU、Unicode 数据及原始附加条款；[完整原始许可](licenses/icu-56.1.txt)，未经修改的源码随桌面对应源码提供 |

Windows 默认 SDK cURL 使用系统 Schannel，不为此链接 OpenSSL。Android 系统 WebView 由操作系统提供，不随 APK 分发 Chromium。Qt AndroidExtras 的许可随 Qt 5 相应模块保留。上表描述本项目使用的版本；改用系统 cURL 或其他 Qt 构建时，必须按实际依赖更新发行材料。

原生桌面构建补充：Linux x86_64 与 macOS Intel 使用官方 Qt 5.15.2，macOS SDK cURL 使用系统 SecureTransport；Linux SDK cURL 使用发行版 Mbed TLS，采用 GPL-2.0-or-later 许可选项。Ubuntu 24.04 ARM64 使用系统 Qt 5.15.13、Qt WebEngine 5.15.16 及系统 Mbed TLS，不随应用复制这些系统库。Linux 包内的 `BUILD-METADATA.json`、`RUNTIME-DEPENDENCIES.txt` 与对应源码材料记录实际系统包及源码版本，不能将其视为使用了表中全部同版本的私有库。QtMacExtras、QtX11Extras 等实际分发模块的原始源码与声明随桌面对应源码保留。

项目的线性 SVG 操作图标为自有资源；不包含外部图标字体。应用图标为本项目生成的视觉资源。小说、封面和插图仅在用户运行时从内容来源获取，不作为源码或安装包素材分发。

## 协议与交互参考

- [MewX/light-novel-library_Wenku8_Android](https://github.com/MewX/light-novel-library_Wenku8_Android)：公开协议、XML 与返回码参考，保留其 GPL v2 来源归属。历史 v1.9 参考提交为 `5fa7ad6e887b227e4c4c207e6c2c09ac2a324b97`。不链接其 Android 库，也不分发其字体、图片或广告组件。
- [Hikari](https://github.com/15dd/hikari_novel_flutter)（MIT）、[niuhuan/wild](https://github.com/niuhuan/wild)（GPL-3.0）、[pywenku8api](https://github.com/WorldObservationLog/pywenku8api)（AGPL-3.0）：用于核对可观察的 URL、表单和页面约定，未复制或链接其代码、资产或二进制。
- [wenku8-opds-readme](https://github.com/WorldObservationLog/wenku8-opds-readme)：OPDS 服务说明参考，未复制或分发其服务实现。
- [KOReader](https://github.com/koreader/koreader)、[Readest](https://github.com/readest/readest)、CoolReader、Lector 和 foliate-js：阅读交互和架构研究参考，未复制、链接或分发它们的代码或资源，不属于本项目运行依赖。

## 对应源码与再分发

源码仓库保留本项目、SDK、平台适配和构建脚本；构建所需的可再分发第三方源码按原始许可证保存。正式二进制应同时提供匹配版本的完整对应源码，包括所分发 Qt 模块、Chromium/Qt 内置组件、实际链接的原生依赖、构建脚本和必要修改说明。仅提供许可证摘要或上游主页链接不能替代适用许可证要求的对应源码。

发布包的 `licenses/` 与源码归档中应保留完整原始声明。使用系统组件、不同 Qt 安装或不同 TLS 后端重新打包时，应重新核对实际分发的组件与源码。Microsoft CRT 等运行库遵循各自再分发条款，不以本项目 GPL 重新许可。

## 原始源码完整性

下列摘要记录原始依赖文件，第三方源文件的版权和许可证不作更改：

```text
curl-8.0.1.zip        9298ef7bfa305c4ea36752a7d44b7aa63aa3074650ab03abad0fe2d7225e514c
libxml2-2.15.4.tar.xz 98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821
tinyxml2.cpp         d5a7fd2ad255d716c4d2a64d5686f90c7e27a8285c49c59a5d105cb400bdf703
tinyxml2.h           02f2389feb67fbb3efe42aaa6942034bc7d54695f357c5aac3cbd2f78a06ca4a
```
