# 鸿蒙客户端

鸿蒙客户端使用 ArkTS Stage 和 ArkUI，通过 N-API 调用仓库中的纯 C++ SDK。工程声明支持 HarmonyOS 的手机、平板和 PC（`2in1`），同一个 ARM64 HAP 包含两种布局。

## 构建未签名 Release HAP

安装带 HarmonyOS 6.1.1（API 24）SDK 的 DevEco Studio 及其 ArkTS、Native、Hvigor 和 OHPM 工具。工程的最低兼容版本为 HarmonyOS 6.0.0（API 20）。在仓库根目录运行以下 PowerShell 命令。

```powershell
pwsh -File platform/harmony/build_release.ps1 -DevEcoStudio 'C:/Program Files/Huawei/DevEco Studio'
```

脚本通过 Hvigor 构建 Release，加入所用依赖的原始许可，并检查 HAP 的应用版本、设备类型、ArkTS 字节码、ARM64 SDK 桥接库和签名状态。默认保存到 `out/harmony`，也可用 `-OutputDirectory` 选择其他目录。开发时可在 DevEco Studio 打开 `platform/harmony`。

构建需要获取 Mbed TLS 3.6.5 的源码归档。可以设置 `WENKU8_HARMONY_MBEDTLS_ARCHIVE` 指定已下载的归档路径，构建仍会核对其固定 SHA-256。

## 发布包与设备验证

发布文件名中的 `arm64-v8a-release-unsigned` 表示 ARM64、Release 构建和未签名状态。未签名 HAP 供后续签名或分发流程使用。真机安装请先通过 DevEco Studio 使用与包名及设备匹配的证书和 Provision Profile 签名，再用 SDK 的 `hdc.exe` 检查已连接设备并安装已签名 HAP。

```powershell
$strHdc = "$env:DEVECO_SDK_HOME/default/openharmony/toolchains/hdc.exe"
& $strHdc list targets
& $strHdc install 'C:/path/to/wenku8-reader-signed.hap'
```

HAP 的设备声明、最低 API 和 ELF 架构可以在构建机检查。验证运行时布局需要 HarmonyOS 6.0.0 或更新版本的 ARM64 设备或兼容的模拟器镜像。PC 检查宽屏布局和缩窄窗口后的切换。手机和平板检查触控布局、横竖屏切换、登录、打开章节及离线续读。
