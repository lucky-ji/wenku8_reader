# iOS 构建与登录验证

iPhone 和 iPad 使用 Qt 5 界面，网页登录由系统 WKWebView 提供。应用内的书架、最近更新和正文请求使用同一登录会话。

## 环境

- Intel Mac 或 GitHub Actions 的 `macos-15-intel`，Xcode 16.4 和 iOS 18.5 Simulator。
- CMake 3.24 或以上，以及 Qt 5.15.2 官方 iOS 套件。此套件包含 x86_64 模拟器与 ARM64 真机切片。
- 如使用 aqtinstall 安装 Qt，先在虚拟环境中安装 `aqtinstall==3.3.0`。环境变量 `QT5_IOS_ROOT` 指向套件目录。

```sh
python3 -m aqt install-qt mac ios 5.15.2 ios --outputdir "$HOME/Qt" \
    --archives qtbase qtsvg qtspeech
export QT5_IOS_ROOT="$HOME/Qt/5.15.2/ios"
```

## 编译与启动

在源代码仓库根目录执行。脚本使用 iOS 15.0 作为最低系统版本，并由仓库中的依赖源码构建静态 SDK。

```sh
bash platform/ios/build.sh simulator
bash platform/ios/build.sh device
```

模拟器和真机分别输出到 `build/ios-simulator` 和 `build/ios-device`。可在 Xcode 中打开构建目录内的 `Wenku8Reader.xcodeproj`，选择 `wenku8_client` 和对应设备运行。真机运行前，在 Xcode 的目标构建设置中将 Code Signing Allowed 设为 Yes，并在 Signing & Capabilities 中选择自己的开发团队。CI 会编译两种目标，并在 iPhone 16 模拟器上启动应用和保存截图。

## 验证登录

1. 启动客户端，进入「我的」并点击「登录文库」。
2. 在弹出的网页登录窗登录。验证通过后窗口应自动关闭，并能查看书架和最近更新。
3. 完全退出并重开客户端。确认后台验证可以恢复登录状态，并检查「退出登录」后可以重新登录。

网页登录验证使用自己的测试账号。GitHub 模拟器启动测试使用新的应用容器。
