# Wenku8 C++ SDK

SDK 是与界面无关的 C++17 库。它实现协议、HTTP 传输抽象、解析、缓存、目录与正文访问、书架和下载存储；不依赖 Qt、Android WebView、Windows 窗口或应用资源。

SDK 使用独立的语义版本。当前库版本为 **1.1.0**，可通过 `Wenku8::sdkVersion()` 查询实际链接的库。

## 目录与边界

- `include/`：四个公开头文件，供调用方使用。
- `src/`：HTTP/cURL、有限重试、网站与历史协议、XML/HTML 解析、文字编码、文件缓存和下载存储实现。`Storage.h`、`WebBackend.h`、`TextPackage.h` 为内部实现，不公开安装。
- `CMakeLists.txt`：定义 SDK 库目标及其必要依赖；不会查找 Qt。

公开接口：

| 头文件 | 责任 |
| --- | --- |
| `Wenku8Sdk.h` | 数据模型、错误、传输/认证抽象、客户端、缓存与传输工厂 |
| `Wenku8Api.h` | 已实现的历史协议请求及解析接口 |
| `Wenku8Catalog.h` | 目录/OPDS 和文本包入口 |
| `Wenku8Download.h` | 可取消、可续传的下载存储与离线读取 |

调用方通过 `HttpTransport` 注入平台会话传输。网页登录、Cookie 生命周期和弹窗属于应用的平台适配层；SDK 不包含 UI、不读取应用窗口或导出浏览器凭据。源站网络故障或网站验证仍会以错误返回，不能将 403 视为请求成功。

## 构建与调用

根工程可以只构建 SDK，也可以直接执行 SDK 独立配置：

```sh
cmake -S sdk -B build-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdk --config Release
```

依赖为 cURL、libxml2、TinyXML-2，非 Windows 平台另需 Iconv。仓库的 `cmake/Dependencies.cmake` 负责统一解析可再分发依赖；上层已提供依赖目标时 SDK 直接复用。Qt 与 Visual Studio 等编译环境不随源码仓库提供，工具链配置参见根 README。

主程序或其他 CMake 工程直接链接独立库目标：

```cmake
add_subdirectory(sdk)
target_link_libraries(your_program PRIVATE Wenku8::Sdk)
```

`WENKU8_BUILD_SHARED=ON` 生成共享库；`OFF` 生成静态库并向调用方传播 `WENKU8_STATIC`。公开接口保留 `WENKU8_API` Windows DLL 导入/导出约定。构建目标自行传播公开头目录，不应让调用方引用 `sdk/src`。

## 启动时配置重试

`Client`、`CatalogClient` 和 `NovelDownloader` 默认自动重试可恢复的只读请求，初次请求加最多两次重试。默认退避从 400 毫秒开始，启用多次尝试时每次最多 12 秒。所有尝试和等待共用请求的 `TransferOptions::m_oTimeout`。重试使用原传输及会话，认证与证书校验失败、请求取消和写入请求不会重新发送。

```cpp
Wenku8::ClientOptions oClientOptions;
oClientOptions.m_strEndpoint = "https://www.wenku8.net";
oClientOptions.m_eProtocol = Wenku8::Protocol::Website;
Wenku8::Client oDefault(oClientOptions);

Wenku8::RetryPolicy oRetry;
oRetry.m_oOptions.m_nMaxAttempts = 5; // 首次请求加最多四次重试
Wenku8::Client oWithRetries(oClientOptions, {}, oRetry);

oRetry.m_bEnabled = false;
Wenku8::Client oSingleAttempt(oClientOptions, {}, oRetry);
```

`m_nMaxAttempts` 可设为 1–10，包含首次请求。注入浏览器或系统传输时可使用 `defaultRetryOptions(RetryProfile::Browser)` 识别没有原生错误码的网络错误。`NovelDownloader` 可用同一策略配置正文和图片，也可分别指定两者。下载时每次尝试遵守请求间隔，429 额外受 `DownloadRequest` 的次数和等待上限约束。要求的等待超过剩余预算时直接返回 429。

直接发送原始请求时可用 `makeSdkTransport` 应用同一策略。一个操作包含多个请求时，可在其外层使用 `makeDeadlineTransport`，让整个操作共享截止时间。未传入策略时会沿用直接注入的 SDK 重试传输。传入策略后会替换该传输的配置。自定义适配器内部若已自行重试，可关闭 SDK 重试。重试回调运行于调用线程，不能重入客户端。

Android NDK r21 的共享 libc++ 缺少 `std::filesystem` 时，可通过 `WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR` 指向匹配的 LLVM 9 filesystem 源码；不得以不匹配的 C++ 运行库替换 Qt 的运行库。

本项目与 SDK 的许可证为 GPL-2.0-only。依赖原始许可证见仓库 `licenses/`，固定版本与源码来源见 [依赖说明](../docs/DEPENDENCIES.md)。
