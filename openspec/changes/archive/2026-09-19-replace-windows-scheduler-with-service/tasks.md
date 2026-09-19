## 1. 实现
- [x] 1.1 移除 Windows schtasks/XML，接入原生 helper 查询与 UAC 管理事务，保留 macOS。
- [x] 1.2 实现无 Qt 依赖 SCM 宿主、LocalService/service SID 权限、启停/回滚/退出码与旧任务迁移。
- [x] 1.3 增加常驻无头计划 worker、持久去重与安全停止，复用同步逻辑。
- [x] 1.4 更新 QML 提示、中英翻译、CMake/部署和回归。

## 2. 验证与交付
- [x] 2.1 Release/Debug 构建、完整 CTest 和宿主 Windows 交叉编译。
- [x] 2.2 严格规格校验、静态审查、说明与临时文件清理，提交。
- [x] 2.3 用户于 2026-09-19 确认本 change 已完成 apply 且 Windows 手测 pass，要求同步归档。
