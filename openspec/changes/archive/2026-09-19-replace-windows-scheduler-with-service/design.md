## Context
旧 Windows 实现依赖 schtasks 读写 XML，用户两次遇到注册失败，要求替换为 SCM 服务。Mac 后端和 CDN 功能不属于撤销范围。

## Decisions
- GUI 与查询维持普通权限，有限管理命令由 ShellExecuteExW 的 runas 启动独立原生 helper，等待退出码反馈取消/拒绝/错误；不把主 GUI 改为 always-admin。
- helper 以静态 CRT/MinGW 运行库构建，无 Qt DLL 依赖；真实 ServiceMain/SCM 状态、STOP/SHUTDOWN 回调。服务账号 LocalService，专属 service SID 仅授予配置/数据库/导出目录写入及 Cookie/可执行与运行时目录读取权限。ACL、配置和服务修改失败须回滚。
- 原生宿主以固定 worker 路径及运行时 PATH 启动 --history-service-worker，避免依赖 Qt Creator 的父进程环境；worker 只创建 QCoreApplication，复用 HistorySyncRunner，不启动 GUI/QML/mpv。
- 每秒检查本地日/周分钟计划，错过不补跑、不主动唤醒；每个日期已尝试标记持久化避免重启或回拨重复。先标记再运行，失败不在同日无限重试。共用数据库锁和 scheduled 审计。
- 服务 STOP 置位传给 worker 的继承事件，取消令牌使同步在当前网络请求后结束，宿主报告 STOP_PENDING；超时才兜底终止，服务不报告虚假成功。
- 配置编辑先生成临时请求，UAC 接受后由 helper 事务应用；取消 UAC 不改变工作计划。卸载保留库、导出、Cookie 和配置。

## Validation
macOS 构建及完整 CTest、无真实服务和用户数据的 SCM 命令替身测试、独立 worker 入口与计划时钟测试。纯 WinAPI 宿主使用本机 MinGW 交叉编译检查。Windows 原生 UAC、服务登录与 Debug DLL 路径、SCM 启停、权限和日志需用户手测；不能以交叉编译代替原生验收。
