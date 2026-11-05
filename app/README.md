# 轻小说文库客户端

客户端通过 `Wenku8::Sdk` 调用文库业务，SDK 源码位于独立的 `sdk/`，不会反向依赖此目录或 Qt。

| 目录 | 职责 |
| --- | --- |
| `ui/` | 主窗口、移动端页面、阅读目录与版本信息页面 |
| `widgets/` | 外观、图标、布局、触控、加载与下拉刷新组件 |
| `reader/` | 正文呈现、阅读模式、阅读位置与书签存储 |
| `services/` | 应用会话协调、封面缓存、网络策略和应用信息 |
| `resources/` | SVG 图标、登录网页样式、许可资源与正式更新日志 |

构建依赖从低到高为 `reader_widgets`、`wenku8_reader`、`reader_platform`、`wenku8_client_services`、`wenku8_client_ui` 和可执行目标 `wenku8_client`。平台登录实现位于 `platform/`，通过 `LoginHost` 抽象向会话服务提供传输与登录能力。

Windows、Linux、macOS 使用 Qt 5 WebEngine；Android 使用系统 WebView。iOS 的适配基础文件已保留，正式 WKWebView 会话实现尚未完成，不能作为完整可用平台发布。工具链与完整构建步骤参见根目录 README。
