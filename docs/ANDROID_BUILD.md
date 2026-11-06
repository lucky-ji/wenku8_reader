# Android 原生依赖构建

应用及 Gradle 打包步骤见 [构建指南](BUILDING.md)。本页说明如何为 Qt 5.15.2 Android 的四个 ABI 准备网络和编码依赖，避免混用主机库或其他架构的静态库。

## 环境

- Python **3.12+**，CMake **3.24+**，Ninja。
- Android NDK **r21e / 21.4.7075529**。本脚本针对项目当前 Qt 5 工具链，固定 API 21、`c++_shared`、Release 与 PIC。
- POSIX shell、GNU make，以及 Autoconf 构建需要的 `sed`、`grep`、`awk`、`sort`、`expr`、`tr`、`cp`、`rm` 等基本 Unix 工具。
- Windows 可以使用 Git Bash 提供 shell 与基本 Unix 工具，但 **Git for Windows 通常不附带 GNU make**，需另行准备兼容的 GNU make，并通过 `--make` 指定。`--shell` 必须明确指向该环境的 `bash.exe` 或 `sh.exe`；不要使用 Windows 的 WSL 启动器。
- Linux/macOS 使用本机 POSIX 工具和匹配的 NDK 主机工具链。脚本提供相应路径选择；本次发行的四 ABI 编译在 Windows 上完成，其他主机未完成构建验收。Apple Silicon 主机使用旧版 Intel NDK 工具可能还需要系统提供相应兼容运行环境。

Windows 脚本调用的是 Windows NDK Clang，而不是 WSL/Linux Clang。不要混合 WSL 的 Linux make 与 Windows NDK，也不要将 MSYS 的宿主 GCC 编译库作为 Android 依赖。shell 自身的安装路径可以有空格；NDK、源码目录、输出目录必须使用**无空白字符**的路径，这是此处 Autoconf/Libtool 流程的限制。

## 准备原始源码

先从 [主仓库](https://github.com/lucky-ji/wenku8_reader) 克隆工程；下列第三方归档的固定下载地址和摘要见 [依赖来源](DEPENDENCIES.md)，不需要从 Release 下载源码包。

将以下未经修改的上游源码目录或原始归档放在同一个目录内，每个组件任选一种：

| 组件 | 解压后的目录名 | 支持的原始归档示例 |
| --- | --- | --- |
| Mbed TLS 3.6.5 | `mbedtls-3.6.5/` | `mbedtls-3.6.5.tar.bz2` |
| cURL 8.0.1 | `curl-8.0.1/` | `curl-8.0.1.zip` |
| GNU libiconv 1.17 | `libiconv-1.17/` | `libiconv-1.17.tar.gz` |

脚本也识别同名 `.tar.gz`、`.tar.bz2`、`.tar.xz`、`.zip`；归档需只有一个顶层源码目录。先由使用者核验来源及上游校验和。脚本检查源码内声明的版本，**版本检查不替代来源或完整性校验**。cURL 原始 ZIP 已随仓库放在 `third_party/`，可以复制到准备目录。

来源与协议见 [第三方说明](../THIRD_PARTY_NOTICES.md)。Mbed TLS 应使用包含其第三方子目录的完整发布源码；源码归档及原始许可应保留以便提供对应源码。脚本不联网下载组件，不附带 Qt、NDK 或签名材料。

## 执行

Windows PowerShell 示例，路径按实际安装位置替换：

```powershell
python platform/android/build_dependencies.py `
  --ndk C:/Android/android-ndk-r21e `
  --source-dir C:/Dependencies/sources `
  --prefix-root C:/Dependencies/wenku8-android `
  --abi all --jobs 8 `
  --shell "C:/Program Files/Git/bin/bash.exe" `
  --make C:/Tools/GnuMake/bin/make.exe `
  --ninja C:/Tools/Ninja/ninja.exe `
  --dry-run
```

`--dry-run` 只验证工具、NDK 版本与源码输入，打印 ABI/API/运行库/构建模式，不创建输出目录、不解压、不编译。移除它后执行构建。单独构建某个 ABI 可将 `all` 替换为 `arm64-v8a`、`armeabi-v7a`、`x86` 或 `x86_64`。

在 Linux/macOS 的 POSIX 终端中：

```sh
python3 platform/android/build_dependencies.py \
  --ndk "$ANDROID_NDK_ROOT" \
  --source-dir "$ANDROID_DEPENDENCY_SOURCES" \
  --prefix-root "$ANDROID_DEPENDENCY_PREFIXES" \
  --abi all --jobs 8
```

`--cmake`、`--ninja`、`--make` 可覆盖默认从 `PATH` 查找的工具。原始输入会复制或解压到输出目录的 `.build/`，配置与编译不会写入原始源码树。不要同时向同一输出目录运行多个脚本实例；更换源码或工具链时使用新的输出目录，避免复用旧 CMake/Autoconf 配置。

## 输出与接入

每个 ABI 独立安装：

```text
<prefix-root>/
├── arm64-v8a/{include,lib}
├── armeabi-v7a/{include,lib}
├── x86/{include,lib}
├── x86_64/{include,lib}
└── .build/                 工作副本、构建目录、各组件日志
```

cURL 使用静态 Mbed TLS 和 HTTP/HTTPS；关闭未提供的 SSH、PSL、IDN、压缩依赖及工具程序。libiconv 仅构建 `libiconv`、`libcharset` 并安装生成的公共头文件，不构建其宿主命令行工具、测试或文档。这些均为 C 库，`c++_shared` 是整个 Android 应用的统一工具链配置，不表示这些 C 库自身需要 C++ 运行库。

脚本结束前检查六个静态库中的 ELF 对象机器类型与所选 ABI 一致，随后打印接入参数。例如在 PowerShell 中：

```powershell
$env:WENKU8_TARGET_DEPS = "C:/Dependencies/wenku8-android/arm64-v8a"
cmake --preset android-arm64-qt5 `
  "-DCURL_DIR=$env:WENKU8_TARGET_DEPS/lib/cmake/CURL" `
  "-DIconv_INCLUDE_DIR=$env:WENKU8_TARGET_DEPS/include" `
  "-DIconv_LIBRARY=$env:WENKU8_TARGET_DEPS/lib/libiconv.a"
```

其他 ABI 的应用构建必须同时将 `ANDROID_ABI` 和依赖前缀改为同一个 ABI，并使用独立构建目录；不能只替换依赖路径。应用仍由项目构建 TinyXML-2、libxml2 及 NDK r21 所需的 filesystem 实现。

首次配置包含较多目标系统检测，耗时可能较长。失败时查看 `.build/<ABI>/logs/` 下对应阶段日志，修复环境后用相同命令继续。依赖编译成功只证明构建与架构检查通过；APK 仍需完成应用编译、Release lint、签名、打包检查和目标设备测试。
