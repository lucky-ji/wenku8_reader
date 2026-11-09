# 轻小说文库

<img src="app/resources/app-icon.png" width="112" alt="轻小说文库图标" />

基于 C++17 与 Qt 5 的 Wenku8 阅读客户端，提供独立的纯 C++ SDK。

[![Version](https://img.shields.io/badge/version-1.0.1-315b46)](docs/CHANGELOG.md)
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

## 1.0.2 开发中的下载与阅读优化

以下功能正在 `feature-reader-downloads` 分支开发，尚未发布；上方下载入口仍为正式版本。

- **下载缓存**：保存在应用数据目录，由应用管理，用于应用内离线阅读。Android 使用内部私有存储，不要求文件管理器或共享目录权限；卸载应用会清除。桌面端使用应用数据目录，设备所有者或管理员仍可访问，并非加密保险箱。
- **下载到本地**：首次操作选择目录，之后自动记住。Android 通过系统目录选择器授予所选目录权限；如权限撤销或目录被移除，可在「下载与存储」重新选择。
- 每本书保存到独立目录，包含 `novel.txt`（UTF-8）与 `index.html`（离线阅读），勾选包含插图时一并保存图片。重名时新建目录，不覆盖已有文件。
- 本地下载优先复用已有完整缓存；缺少内容时联网补全到临时空间。仅下载到本地不会将该书加入缓存书库，两者可分别使用。
- 下载使用复用当前登录状态的独立网页会话，与前台浏览和阅读分开请求；两种下载均使用后台队列，不锁定页面；在「下载缓存」查看缓存与本地下载的进度、暂停、继续和移除记录，完成时显示自动消失且不拦截点击的浮动提示。
- 每个已完成章节和图片均持久化保存。程序异常退出后，重新打开并验证原账号即可自动恢复运行中及排队任务；手动暂停的任务保持暂停。断点为章节/图片级，尚未下载完整的单次响应会重新请求。
- Android 系统休眠或结束进程时，任务可能被暂停；本功能不承诺进程结束后仍持续联网，重新打开应用会按断点继续。
- 阅读设置新增行距、段落间距与页边留白，设置自动保存，排版变化保留原阅读位置。

## 平台

| 平台 | 状态 | 登录组件 |
| --- | --- | --- |
| Windows x64 / x86 | 安装包、便携包构建与校验通过 | Qt 5 WebEngine |
| Android ARM64 / ARMv7 / x86 / x86_64 | Release 构建与架构检查通过 | Android 系统 WebView |
| Linux x86_64 | Ubuntu 22.04 原生构建与启动检查通过 | Qt 5 WebEngine |
| Linux ARM64 | Ubuntu 24.04 原生构建与启动检查通过 | 系统 Qt 5 WebEngine |
| macOS Intel x86_64 | 原生构建、部署与 WebEngine 启动检查通过 | Qt 5 WebEngine |
| iOS | 登录适配尚未完成，不提供占位客户端 | WKWebView 适配待实现 |

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

先从主仓库获取工程：

```sh
git clone https://github.com/lucky-ji/wenku8_reader.git
cd wenku8_reader
```

默认获取 `main` 分支。若需对应正式版本的工程代码，可执行 `git switch --detach v1.0.1`；最新环境与构建说明以主仓库的 [构建指南](https://github.com/lucky-ji/wenku8_reader/blob/main/docs/BUILDING.md) 为准。

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

第三方依赖的版本、源码地址和摘要见 [依赖来源](docs/DEPENDENCIES.md)。Release 手动附件只保留编译好的发布包。

Android 的 Qt、NDK、JDK 和目标原生依赖必须匹配，四 ABI 可使用 [依赖构建脚本](docs/ANDROID_BUILD.md) 分别准备。Linux/macOS 的原生构建与部署见 [原生桌面指南](docs/NATIVE_DESKTOP.md)。完整配置、APK 生成与打包说明见 [构建指南](docs/BUILDING.md)。

Linux 包依赖对应 Ubuntu 的系统运行库，不能视为所有发行版通用包；macOS 包使用本机完整性签名，尚未进行 Apple 公证。原生 Windows ARM64 与 macOS ARM64 缺少本项目可用的 Qt 5 WebEngine kit。构建和启动检查不代替真实设备、账号及源站环境的业务测试。

## 反馈与后续计划

问题和建议请通过 [GitHub Issues](https://github.com/lucky-ji/wenku8_reader/issues/new) 提交。应用内「我的」、桌面端「帮助」菜单和「版本说明」也提供意见反馈及开源项目入口。

- **1.0.x**：持续优化阅读体验、触控交互、下载和离线使用，以及跨平台稳定性。
- **1.1.0 计划**：增加本地文件阅读，让用户可以打开自己保存的电子书。
- **后续大版本计划**：扩展其他书源，并逐步完善相应的浏览与阅读体验。

以上为后续规划，尚未实现的功能不属于当前版本能力，具体范围与时间以实际发布说明为准。当前的「下载到本地」仅用于保存 Wenku8 小说，不代表已经支持导入或阅读本地文件。

## 许可与致谢

本应用与 SDK 按 [GPL-2.0-only](LICENSE) 分发。第三方组件保留各自版权和许可证；详细来源、适用平台及对应源码安排见 [第三方声明](THIRD_PARTY_NOTICES.md)。

协议研究参考 [MewX/light-novel-library_Wenku8_Android](https://github.com/MewX/light-novel-library_Wenku8_Android) 及其 [项目主页](https://wenku8.mewx.org/)。本项目是独立客户端，与 Wenku8 网站及参考项目无隶属关系。小说内容的权利归各自权利人，源码与安装包不包含小说、个人书架或账号数据。

开发者：[lucky_ji](https://github.com/lucky-ji)。

