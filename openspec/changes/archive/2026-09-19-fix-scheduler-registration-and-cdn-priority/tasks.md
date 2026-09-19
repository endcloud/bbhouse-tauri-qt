## 1. 实现与回归
- [x] 1.1 原 Windows XML 修复已由 `replace-windows-scheduler-with-service` 替代，不再保留其增量规格或验收项。
- [x] 1.2 统一 UGC/PGC DASH、durl 和直播 CDN 优先级，补离线 fixture。
- [x] 1.3 接入完整音频/durl 候选链及异步失败、超时与旧回调隔离测试。

## 2. 验证与交付
- [x] 2.1 Release 主程序与全部回归目标构建、完整 CTest。
- [x] 2.2 Qt Creator Debug 构建、OpenSpec strict 校验与 diff 检查。
- [x] 2.3 记录根因、实现边界和手测步骤，清理临时文件并提交。
- [x] 2.4 用户于 2026-09-19 确认本 change 已完成 apply 且 Windows 手测 pass，要求同步归档；验收范围为当前媒体 CDN 变更，旧 XML 方案已被替代。
