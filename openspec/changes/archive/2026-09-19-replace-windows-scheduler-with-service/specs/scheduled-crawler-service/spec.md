## MODIFIED Requirements

### Requirement: 每日定时触发
系统 SHALL 支持每天或每周指定星期，在本地 HH:mm（默认每天 01:00）由平台原生任务触发。macOS 使用当前用户 LaunchAgent，Windows 使用 LocalService 身份的原生 Windows Service，可在未登录时运行；macOS 要求登录会话，主应用可关闭。Windows 不补跑错过时刻，macOS 休眠唤醒遵循 launchd 合并触发规则，并在界面说明。

#### Scenario: 关闭主窗口后触发
- **WHEN** 用户已登录且到达已启用任务的时间/周期
- **THEN** 系统启动一次无头同步，不打开 GUI 或播放器。

#### Scenario: 到点触发
- **WHEN** 配置时间为 01:00 且服务运行中，系统时间到达 01:00
- **THEN** 服务开始执行一次全量爬取，当日该时刻不再重复触发

#### Scenario: 错过时段不补跑
- **WHEN** Windows 配置时刻关机或休眠错过，或 macOS 休眠跨过时刻
- **THEN** Windows 等待下一次计划；macOS 按 launchd 唤醒合并触发，界面说明平台差异。

### Requirement: 服务注册与注销机制
Windows SHALL 使用 SCM 服务；查询无需提权，注册、修改、暂停/启用与卸载 SHALL 经 UAC 确认；macOS 沿用当前用户 LaunchAgent，无需管理员。权限/策略拒绝、超时或取消 SHALL 明确报错且查询失败不得当成未注册。注册/更新失败 SHALL 恢复旧配置和任务；回滚失败须明确报告实际不一致状态。注销保留数据与配置。

#### Scenario: 权限拒绝
- **WHEN** 平台拒绝注册或修改任务
- **THEN** 显示权限错误，不宣称成功，保留可恢复配置并重新核对状态。

#### Scenario: 查询注册状态免提权
- **WHEN** 主应用以普通权限运行并检测服务注册状态
- **THEN** 检测完成，全程无 UAC 弹窗

#### Scenario: 拒绝提权不残留
- **WHEN** 用户取消 Windows UAC，或操作被系统策略拒绝
- **THEN** 报告拒绝，恢复此前配置和注册状态；回滚失败单独明确显示。

## ADDED Requirements

### Requirement: Windows 原生服务宿主与无头计划
Windows SHALL 使用实现 ServiceMain 与 SCM 控制处理的原生独立宿主，作为 LocalService 配合专属服务 SID 运行。宿主 SHALL 固定 Qt worker 与运行时路径，在 Session 0 启动仅 QCoreApplication 的常驻计划入口。计划 SHALL 每日或每周在本地目标分钟执行一次，持久记录已尝试日期并复用原有同步 runner、锁、限速和审计。安装、更新失败 MUST 尝试恢复配置、ACL 和注册状态；UAC 取消 MUST 不改变有效计划。

#### Scenario: Qt Creator Debug 安装
- **WHEN** 用户从 Qt Creator Debug 确认 UAC 并安装服务
- **THEN** SCM 启动无 Qt DLL 依赖的宿主，以固定运行时路径启动 worker，不依赖 Qt Creator 持续运行。

#### Scenario: 停止及注销
- **WHEN** 用户经 UAC 停止或注销服务
- **THEN** 宿主请求 worker 取消，同步收尾后报告实际停止；数据库、导出、Cookie 与配置保留。

#### Scenario: 重启和时钟回拨
- **WHEN** 当日计划已尝试后服务重启或本地时钟回拨到目标分钟
- **THEN** 不重复同步；关机或休眠错过的分钟不补跑。
