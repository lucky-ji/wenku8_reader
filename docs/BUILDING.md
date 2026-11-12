# 构建与打包

## 获取工程源码

```sh
git clone https://github.com/lucky-ji/wenku8_reader.git
cd wenku8_reader
```

默认获取 `main` 分支。若需对应正式版本的工程代码，可执行 `git switch --detach v1.0.3`；最新环境与构建说明以主仓库的 [构建指南](https://github.com/lucky-ji/wenku8_reader/blob/main/docs/BUILDING.md) 为准。

工程与 SDK 源码、平台适配、构建脚本及许可证均在主仓库中。Qt、NDK 等环境另行安装；未随仓库保存的第三方依赖源码按 [依赖来源](DEPENDENCIES.md) 获取，不从 Release 附件取得工程源码。

## 通用要求

- CMake 3.24 或更高版本、Ninja、支持 C++17 的编译器。
- 客户端使用 Qt 5，工具链基线为 5.15.2，包含 Widgets、Svg、Concurrent；桌面另需 Network、WebEngineWidgets 及其传递依赖，Android 另需 AndroidExtras。Linux ARM64 使用 Ubuntu 的 Qt 5 包，实际版本记录在发行材料中。
- SDK 依赖 cURL 7.85+、TinyXML-2、libxml2，非 Windows 平台另需 Iconv。仓库保留 TinyXML-2 源码与 libxml2/cURL 原始归档；`WENKU8_BUNDLED_CURL=ON` 使用仓库的 cURL 源码，`OFF` 使用目标环境提供的 cURL。
- Qt、Visual Studio、Android SDK/NDK、JDK、签名工具等开发环境不随仓库提供。第三方源码与许可证保持原样。

在仓库根目录执行命令。所有构建输出写入 `build/`，部署输出写入 `out/`。

## Windows x64

安装 MSVC x64 C++ 工具链及 Windows SDK，使用与 Qt 5.15.2 `msvc2019_64` kit 兼容的编译器和运行库。可使用 Visual Studio 2019 或更新版本的兼容 MSVC 工具链。请在相应的 x64 开发者终端中运行 CMake，避免与 MinGW 库混用。

```powershell
$env:QT5_ROOT = "C:/Qt/5.15.2/msvc2019_64"
cmake --preset windows-qt5
cmake --build --preset windows-qt5 --parallel
```

默认使用本地 cURL 源码并启用 Windows Schannel；无需为 SDK 单独安装 OpenSSL。可执行文件由 CMake 输出在 `build/windows/app/`。运行完整客户端还需要部署匹配的 Qt DLL、平台插件、图片插件、WebEngine 子进程与资源。

```powershell
cmake --install build/windows --prefix out/windows
& "$env:QT5_ROOT/bin/windeployqt.exe" --release out/windows/bin/wenku8_client.exe
```

部署时还需包含 SDK 动态库、实际依赖运行库、`licenses/` 和对应源码材料。检查 `windeployqt` 的实际结果，不能只复制主程序 exe。Qt 某些二进制构建的 WebEngine/网络依赖 OpenSSL，即使 SDK 使用 Schannel，也应按实际 Qt 运行组件部署并保留来源。

MSI 脚本使用 Python 3 与 WiX Toolset 3。先准备完整的部署目录：

```powershell
python platform/windows/build_msi.py --stage out/windows/bin --wix C:/Tools/WiX/bin --output out/installer --version 1.0.3
```

`--stage` 指向经过检查的应用根目录；将许可证、图标及其他分发文件与程序一起放在该目录。发布附件仅包含编译好的程序；源码入口为主仓库及 [匹配依赖来源](DEPENDENCIES.md)。保留完整许可证、实际依赖版本及必要构建信息，不把开发机的 Qt 或 VS 安装目录加入 Git。

## Android ARM64

当前 Qt 5 工具链基线：

| 组件 | 版本或配置 |
| --- | --- |
| Qt | 5.15.2 Android，包含 ARM64、Svg、Concurrent、AndroidExtras |
| NDK | r21e，与 Qt kit 保持一致 |
| JDK | 11 |
| SDK | platform android-28、build-tools 28.0.3、platform-tools |
| Gradle / Android Gradle Plugin | 5.6.4 / 3.6.0，由 Qt 部署模板与项目配置使用 |
| 原生 ABI / API | arm64-v8a / android-21，`c++_shared` |
| 应用 ID | `io.github.lucky_ji.wenku8_reader` |

主界面为 Qt Widgets，登录使用设备系统 WebView；不使用 Qt 6 或桌面 WebEngine。minSdk/targetSdk 描述工具链构建配置，不等于所有系统版本均已验收。应用商店的 targetSdk 要求可能需要单独升级工具链后适配。

在本机设置以下路径，示例路径按各自安装位置替换：

```powershell
$env:QT5_ANDROID_ROOT = "C:/Qt/5.15.2/android"
$env:ANDROID_SDK_ROOT = "C:/Android/sdk"
$env:ANDROID_NDK_ROOT = "C:/Android/sdk/ndk/21.4.7075529"
$env:JAVA_HOME = "C:/Java/jdk-11"
$env:WENKU8_TARGET_DEPS = "C:/Dependencies/wenku8-android-arm64"
cmake --preset android-arm64-qt5
cmake --build --preset android-arm64-qt5 --parallel
cmake --build --preset android-apk-qt5
```

`WENKU8_TARGET_DEPS` 必须包含针对同一 NDK、ABI、API 与运行库构建的 cURL、Mbed TLS、libiconv/libcharset。它不是 Windows 或主机依赖目录。优先使用 cURL 导出的 `CURLConfig.cmake`，让静态 TLS 依赖正确传递；必要时显式设置 `CURL_DIR`、`Iconv_INCLUDE_DIR` 和 `Iconv_LIBRARY`。TinyXML-2 与 libxml2 由项目构建。

原生依赖的构建原则：

- Mbed TLS 3.6.5：相同 Android CMake toolchain、ARM64/API21、Release、PIC；关闭测试和示例程序，安装静态库。本项目采用 GPL-2.0-or-later 许可选项。
- cURL 8.0.1：相同 toolchain，静态库、HTTP-only、Mbed TLS 后端；关闭命令行程序、测试以及未提供的 SSH、PSL、IDN、压缩依赖。导出 CMake 包配置。
- GNU libiconv 1.17/libcharset：交叉编译目标 `aarch64-linux-android`，NDK Clang 的 API21 target，静态库、PIC；不得链接主机 iconv。

NDK r21 的共享 libc++ 未提供完整 `std::filesystem` 实现，预设使用 `third_party/libcxx-filesystem` 中未修改的匹配 LLVM 9.0.1 源码补足实现，不替换 Qt 的 C++ 运行库。如果切换到已经包含 filesystem 的受支持工具链，可配置 `-DWENKU8_ANDROID_FILESYSTEM_SOURCE_DIR=`；更换 NDK 后需要重新验证 Qt 和全部原生依赖兼容性。

需要手动部署时，Qt 5 会在构建目录生成 `android_deployment_settings.json`。例如：

```powershell
& "$env:QT5_ANDROID_ROOT/bin/androiddeployqt.exe" --input build/android-arm64/android_deployment_settings.json --output build/android-arm64/android-build --android-platform android-28 --jdk "$env:JAVA_HOME" --gradle
```

APK 输出位于部署目录的 Gradle `outputs/apk/` 下。正式发布应使用开发者自持密钥签名，并检查对齐与签名；密钥、密码和本地签名配置不进入仓库。新应用 ID 与旧开发包不同，安装后需要在应用内登录。

## 仅 SDK 与其他桌面平台

SDK 不需要 Qt：

```sh
cmake -S . -B build/sdk -DWENKU8_BUILD_CLIENT=OFF
cmake --build build/sdk --config Release
cmake --install build/sdk --config Release --prefix out/sdk
```

使用系统 cURL 时设置 `-DWENKU8_BUNDLED_CURL=OFF`；静态 SDK 设置 `-DWENKU8_BUILD_SHARED=OFF`。Linux/macOS 的 `desktop-qt5` 预设使用 `QT5_ROOT` 和可选 `WENKU8_TARGET_DEPS`，需要系统提供满足最低版本的目标 cURL 和 Iconv。发行构建的 Qt、TLS 后端、系统依赖及原生构建流程见 [原生桌面构建](NATIVE_DESKTOP.md)；编译和启动检查不代替真实账号与设备的业务验收。

iOS 仅保留启动界面与基础配置，WKWebView 会话适配尚未实现。当前不提供 iOS 客户端构建预设，不能将现有目录视为可发布版本。

## 发布核对

1. 在干净构建目录生成程序，核对版本为 1.0.3、名称为轻小说文库。
2. 检查首次登录、重启会话恢复、书架、搜索、正文、目录、下载与离线阅读。
3. 检查明暗护眼主题、历史位置、书签，以及 Android 触控和音量键。
4. 打包完整运行依赖、原始许可证与组件声明，核对主仓库中的版本源码、依赖来源及必要构建信息。
5. 检查包内不含账号、Cookie、正文缓存、历史、密钥或开发工具；更新 SHA-256 摘要并发布正式资产。

## 架构选择

各架构使用独立的 Qt kit、依赖前缀、构建目录和安装包，不能混用不同位数的 DLL 或静态库。

| 目标 | 配置预设 | 环境要求 |
| --- | --- | --- |
| Windows x64 | `windows-qt5` | x64 MSVC 开发者终端，`QT5_ROOT` 指向 `msvc2019_64` |
| Windows x86 | `windows-x86-qt5` | x86 MSVC 开发者终端，`QT5_X86_ROOT` 指向 `msvc2019` |
| Android ARM64 | `android-arm64-qt5` | `WENKU8_TARGET_DEPS` 指向 `arm64-v8a` 前缀 |
| Android ARMv7 | `android-armv7-qt5` | 依赖前缀为 `armeabi-v7a` |
| Android x86 | `android-x86-qt5` | 依赖前缀为 `x86` |
| Android x86_64 | `android-x86_64-qt5` | 依赖前缀为 `x86_64` |

Windows x86 打包时为 `build_msi.py` 添加 `--arch x86`，由 WiX 生成对应位数的安装包，并部署同位数的 Qt 与 Visual C++ 运行库；x64 使用默认 `--arch x64`。两种安装包采用独立的安装目录与升级标识。

Android 四 ABI 的网络与编码依赖可使用 [原生依赖构建脚本](ANDROID_BUILD.md) 准备。选定预设后先执行 `cmake --preset <预设名>`，再执行 `cmake --build --preset <预设名>`；生成 APK 时为构建命令添加 `--target apk`，或使用 `CMakePresets.json` 中对应的 APK 构建预设。每次都应让 `ANDROID_ABI` 与依赖前缀一致。

Linux x86_64、Linux ARM64 与 macOS Intel 的原生 runner、构建配置、部署、运行依赖、签名及对应源码安排见 [NATIVE_DESKTOP.md](NATIVE_DESKTOP.md)。Windows ARM64 和原生 macOS ARM64 尚无本项目可用的 Qt 5 WebEngine kit；iOS 登录适配尚未完成，因此不生成占位客户端。
