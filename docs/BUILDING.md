# 构建与打包

## 通用要求

- CMake 3.24 或更高版本、Ninja、支持 C++17 的编译器。
- 客户端使用 Qt 5.15.2，包含 Widgets、Svg、Concurrent；桌面另需 Network、WebEngineWidgets 及其传递依赖，Android 另需 AndroidExtras。
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
python platform/windows/build_msi.py --stage out/windows/bin --wix C:/Tools/WiX/bin --output out/installer --version 1.0.0
```

`--stage` 指向经过检查的应用根目录；将许可证、图标及其他分发文件与程序一起放在该目录。发布时应生成并随附与二进制一致的对应源码归档，而不是把开发机的 Qt 或 VS 安装目录加入 Git。

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

使用系统 cURL 时设置 `-DWENKU8_BUNDLED_CURL=OFF`；静态 SDK 设置 `-DWENKU8_BUILD_SHARED=OFF`。Linux/macOS 的 `desktop-qt5` 预设使用 `QT5_ROOT` 和可选 `WENKU8_TARGET_DEPS`，需要系统提供目标 cURL 和 Iconv；这些平台尚未完成发行验收。

iOS 仅保留启动界面与基础配置，WKWebView 会话适配尚未实现。当前不提供 iOS 客户端构建预设，不能将现有目录视为可发布版本。

## 发布核对

1. 在干净构建目录生成程序，核对版本为 1.0.0、名称为轻小说文库。
2. 检查首次登录、重启会话恢复、书架、搜索、正文、目录、下载与离线阅读。
3. 检查明暗护眼主题、历史位置、书签，以及 Android 触控和音量键。
4. 打包完整运行依赖、原始许可证、组件声明和匹配的对应源码。
5. 检查包内不含账号、Cookie、正文缓存、历史、密钥或开发工具；更新 SHA-256 摘要并发布正式资产。

## 对应源码归档

使用 `platform/windows/package_sources.py` 与 `platform/android/package_sources.py` 生成正式源码包。工具从已核实的匹配依赖归档复用第三方源文件，忽略其中旧的 `project/` 内容，再加入当前 SDK、客户端、平台适配、CMake、编码规范、许可证与文档白名单。工具不删除旧归档，输出必须使用新路径。

Windows 示例：

```powershell
python platform/windows/package_sources.py --previous C:/ReleaseSources/matching-qt5152-windows.zip --qt-source C:/Qt/5.15.2/Src --openssl-source C:/ReleaseSources/openssl-1.1.1t.tar.gz --zlib-source C:/ReleaseSources/zlib-1.2.13.tar.gz --output out/releases/v1.0.0/wenku8-reader-1.0.0-windows-sources.zip
```

Android 示例可复用相同的完整 Qt 5.15.2 依赖源归档，但该归档还需包含 Mbed TLS 3.6.5 和 libiconv 1.17 的原始源码压缩包：

```powershell
python platform/android/package_sources.py --previous C:/ReleaseSources/matching-qt5152-and-android-dependencies.zip --qt-source C:/Qt/5.15.2/Src --output out/releases/v1.0.0/wenku8-reader-1.0.0-android-sources.zip
```

`--qt-source` 只加入 Qt 超级工程的配置文件；各 Qt 模块的完整对应源码来自 `--previous`。Windows 保留 WebEngine 及 Chromium 源码，补齐实际 Qt 依赖的 OpenSSL/zlib 原始源码。Android 选择完整 qtbase、qtsvg、qtimageformats、qtandroidextras，加上 Mbed TLS/iconv；Android 系统 WebView 不随 APK 分发，因此不附带 Chromium 或 OpenSSL。Qt 原始源码中的测试和夹具保持原样；应用自己的旧测试、调试程序、旧目录、构建输出、Qt 开发环境及用户数据不进入源码包。

可用 `--stage` 指定部署目录，额外提取 Qt 组件原始归属到 `licenses/qt-components/`。每个输出包含 `SOURCE-README.txt` 和 `SOURCE-MANIFEST.json`，并在打包后核验 ZIP CRC、必要源码路径和项目目录白名单，生成旁置 SHA-256 文件。修改正式源码后必须重新生成对应源码包，使其与最终二进制一致。
