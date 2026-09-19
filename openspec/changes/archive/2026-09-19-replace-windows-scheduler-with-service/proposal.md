## Why
Windows 任务计划注册在文件句柄修复后仍报 XML 无法切换编码。用户明确取消相关实现，改用 Windows Service 和 UAC 确认。本次移除 Windows schtasks/XML 后端；上轮媒体 CDN 改动保留。

## What Changes
- Windows 原生 SCM 服务，安装/修改/启停/卸载经 UAC，查询无需提权。
- 原生无 Qt 依赖宿主在 Session 0 启动现有程序的无 GUI 常驻同步入口，按日/周执行，停止可取消。
- 默认 LocalService + 服务 SID 最小文件访问权限，不以 LocalSystem 读取可变用户配置运行代码。
- 固定运行时路径以兼容 Qt Creator Debug；保留历史库、Cookie 路径和 macOS LaunchAgent。
- 更新 UI/翻译、隔离回归与 Windows 手测说明。原生 Windows 运行已于 2026-09-19 由用户确认手测通过。

## Impact
scheduled-crawler-service、service-control-ui；新增 Windows 宿主及 worker，更新打包和测试。取消 UAC 不修改有效配置或服务，失败需恢复并报告实际状态；已有旧任务仅迁移移除本实例，不操作无关任务。
