# 轻小说文库

<img src="app/resources/app-icon.png" width="112" alt="轻小说文库图标" />

基于 C++17 与 Qt 5 的 Wenku8 阅读客户端，提供独立的纯 C++ SDK。

[![Version](https://img.shields.io/badge/version-1.0.0-315b46)](docs/CHANGELOG.md)
[![License](https://img.shields.io/badge/license-GPL--2.0--only-blue)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-5.15-41cd52)](https://doc.qt.io/archives/qt-5.15/)

[下载与发布](https://github.com/lucky-ji/wenku8_reader/releases) · [构建指南](docs/BUILDING.md) · [SDK 架构](docs/ARCHITECTURE.md) · [更新日志](docs/CHANGELOG.md) · [开源许可](THIRD_PARTY_NOTICES.md)

## 功能

- 登录并保存本机会话，登录状态检查完成后进入主界面。
- 书架、小说发现与搜索、书籍详情、卷与章节目录、加入书架。
- 正文与插图阅读，滚动和翻页模式，移动端音量键翻页。
- 每本书独立保存阅读位置，支持阅读历史与书签。
- 下载与离线阅读，封面缓存，网络失败重试与保留已有内容。
- 明亮、暗色、护眼三种外观，桌面与触屏分别适配。

## 平台

| 平台 | 状态 | 登录组件 |
| --- | --- | --- |
| Windows x64 | 1.0.0 发行目标 | Qt 5 WebEngine |
| Android ARM64 | 1.0.0 发行目标 | Android 系统 WebView |
| Linux / macOS | 保留桌面构建路径，尚未完成平台验收 | Qt 5 WebEngine |
| iOS | 尚未完成，不提供可用客户端 | WKWebView 适配待实现 |

应用首次使用时按需打开登录窗口；登录成功后关闭网页，书架、阅读与其他业务通过 Qt 界面操作。网站可用性及验证要求取决于源站，客户端会显示实际错误，不将验证页视为业务数据。

## 工程结构

```text
sdk/                 纯 C++ SDK：协议、数据解析、HTTP 抽象、缓存、下载
app/ui/              桌面与移动端界面
app/widgets/         外观、图标、触控与加载组件
app/reader/          正文呈现、阅读位置与书签
app/services/        应用会话、封面与网络协调
app/resources/       图标、登录样式、版本与许可资源
platform/            平台登录、JNI 与打包适配
cmake/               构建与依赖配置
third_party/         构建所需的第三方源码及原始声明
docs/                构建、架构与正式更新记录
licenses/            完整第三方许可证
```

SDK 不依赖 Qt、JNI 或阅读器界面。主程序链接 `Wenku8::Sdk` 库，由平台适配层实现登录与会话传输。需要仅使用 SDK 时可以关闭客户端构建，详见 [架构与 SDK 接入](docs/ARCHITECTURE.md)。

## 从源码构建

需要 CMake 3.24+、支持 C++17 的编译器。客户端仅使用 Qt 5.15；Qt、Visual Studio、Android SDK/NDK 和 JDK 均由开发者自行安装，不包含在源码仓库中。

Windows 在匹配 Qt 的 MSVC x64 开发者终端中执行：

```powershell
$env:QT5_ROOT = "C:/Qt/5.15.2/msvc2019_64"
cmake --preset windows-qt5
cmake --build --preset windows-qt5
```

仅构建 SDK：

```sh
cmake -S . -B build/sdk -DWENKU8_BUILD_CLIENT=OFF
cmake --build build/sdk --config Release
```

Android 的 Qt、NDK、JDK 和目标原生依赖必须匹配。完整配置、APK 生成与打包说明见 [构建指南](docs/BUILDING.md)。

## 许可与致谢

本应用与 SDK 按 [GPL-2.0-only](LICENSE) 分发。第三方组件保留各自版权和许可证；详细来源、适用平台及对应源码安排见 [第三方声明](THIRD_PARTY_NOTICES.md)。

协议研究参考 [MewX/light-novel-library_Wenku8_Android](https://github.com/MewX/light-novel-library_Wenku8_Android) 及其 [项目主页](https://wenku8.mewx.org/)。本项目是独立客户端，与 Wenku8 网站及参考项目无隶属关系。小说内容的权利归各自权利人，源码与安装包不包含小说、个人书架或账号数据。

开发者：[lucky_ji](https://github.com/lucky-ji)。

