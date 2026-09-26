## Why

Windows Qt Creator 在链接 `bbhouse-history-service.exe` 时失败，末行仅显示 `build.make:106: Error 1`。本机复现后的真实错误为 `cannot open output file ... Permission denied`，SCM 中正在运行的服务使用的正是该 Debug 构建目录的宿主及 worker。

解除占用后还发现 MinGW Makefiles 不能识别中文文件名的帮助文档资源依赖，主程序构建继续失败；并遇到 C 盘空间不足的独立环境问题。

## What Changes

- 核实并释放开发构建目录的进程占用，重新验证原 Qt Creator 构建和离线回归。
- 在 Windows 构建入口补充诊断及恢复步骤，解释关闭 GUI 不能停止常驻服务。
- 将帮助 Markdown 通过 CMake 暂存为 ASCII 文件名，保留原中文资源 URL 和文档单一来源。
- 记录本机原生验证结果；无需修改 C++、链接选项、权限或服务运行契约。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

无。本次为环境故障排查、构建修复及文档维护，使用 `skip_specs: true`，沿用 `scheduled-crawler-service` 的 SCM/UAC 与正常停止契约和现有帮助资源契约。

## Impact

影响 `app/CMakeLists.txt` 的资源输入、Windows 开发构建操作说明和本次 OpenSpec 工作记录。暂停服务由用户完成；不卸载真实服务，不修改用户 ACL、Cookie 或数据库，不自动启动真实同步。
