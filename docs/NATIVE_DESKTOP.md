# 原生 Linux 与 macOS 构建

手动工作流 `Native Qt 5 desktop builds` 仅构建和保存 Actions artifacts，不创建标签、不修改 Release、不自动发布。

| 目标 | 原生 runner | Qt 来源 | SDK TLS |
| --- | --- | --- | --- |
| Linux x86_64 | Ubuntu 22.04 | 官方 Qt 5.15.2 gcc_64，含 WebEngine | OS Mbed TLS，选择 GPL-2.0-or-later 选项 |
| Linux ARM64 | Ubuntu 24.04 ARM64 | Ubuntu 实际提供的 Qt 5/WebEngine 包，准确版本写入 metadata | OS Mbed TLS，选择 GPL-2.0-or-later 选项 |
| macOS x86_64 | macos-15-intel | 官方 Qt 5.15.2 clang_64，含 WebEngine | 系统 SecureTransport |

这些是构建目标，最终是否可以分发以各原生任务的构建、部署与校验结果为准。Qt 5.15.2 没有官方 macOS ARM64 WebEngine kit，本工作流不生成虚构的 ARM64 Mac 客户端。iOS 不在本工作流范围内。

## 运行

在 GitHub Actions 中选择该工作流，使用 `Run workflow` 指定源码 ref；或使用：

```sh
gh workflow run native-desktop.yml --ref main -f ref=main
```

工作流检查出指定 ref，分别原生编译完整 SDK、Qt UI、桌面登录与阅读器。CMake 必须找到真实 Qt 5 WebEngine，缺失时直接失败。Linux 通过 Xvfb 启动部署后的客户端并检查 WebEngine 辅助进程；macOS 验证部署后的主程序、WebEngine helper 架构和签名，并执行版本入口。这些检查不代替真实账号、源站和实体设备的业务验收。

每个平台产生 binary 和 provenance 两个 artifact。`BUILD-METADATA.json` 记录实际 commit、Git tree、架构、Qt 版本、Qt 部署模块与系统运行库版本。即使后续仅整理 Git 提交信息，也可以用 tree 比对确认构建源码是否保持一致。

## 安装与运行边界

Linux 以 `.tar.gz` 提供。解压后运行 `wenku8_reader`；`RUNTIME-DEPENDENCIES.txt` 列出所需 Ubuntu OS 运行库。x86_64 包含实际需要的官方 Qt 私有库、插件、WebEngine helper 与资源；ARM64 使用 Ubuntu 系统 Qt，不随包复制 Qt 库。两种包均不复制 OS 库，不能将其视为任意 Linux 发行版通用的静态二进制。

macOS 以 `.zip` 提供应用 bundle。部署使用 Qt 5 的 `macdeployqt`，包含真实 WebEngine 运行组件。构建使用 ad-hoc 签名校验包内完整性，没有 Apple Developer ID 签名或公证，不宣称已经满足 Mac App Store 分发要求。

## 源码与许可

provenance artifact 包含精确 `project.tar.gz`、build/tree 标识、许可证和依赖来源。官方 Qt kit 的完整对应源码在正式发布时由发布流程合并匹配的 Qt 5.15.2 原始模块与组件声明，包括实际使用的 qtx11extras、qtmacextras 等；不能把 provenance artifact 单独称为完整 Qt 对应源码包。上游完整 Qt 5.15.2 源码摘要已记录在 artifact。

Linux ARM64 的系统 Qt 版本不假定为 5.15.2；实际 OS 包和 source package 版本记录在 `UBUNTU-SOURCE-VERSIONS.json`。由于这些 Qt 和 OS 库没有随二进制复制，它们作为明确的系统前置依赖处理。系统组件版权文件随 provenance 保留。若后续改为把 OS/系统 Qt 库一起打包，必须另行附上逐版本匹配的完整源码和原始声明。
