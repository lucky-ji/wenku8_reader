# 平台适配

`LoginHost.h` 是客户端登录窗口与会话传输的边界；它属于 Qt 应用适配层，不进入纯 C++ SDK。

- `desktop/`：Qt 5 WebEngine 会话传输与桌面登录窗口。
- `android/`：正式 AndroidManifest、Gradle 配置、Java WebView/资源传输、JNI 与音量键适配。应用 ID 为 `io.github.lucky_ji.wenku8_reader`。
- `windows/`：Windows MSI、图标及对应源码分发脚本。
- `ios/`：启动界面与 Info.plist 基础配置；WKWebView 会话适配尚未完成。

平台库只依赖 SDK、公共 Qt 组件与 `reader_widgets`，不依赖主窗口或会话服务。Android 公共资源传输实现虽存放在平台目录，但编译到应用网络组件中，以避免静态库循环依赖。
