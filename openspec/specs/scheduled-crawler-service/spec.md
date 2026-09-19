# scheduled-crawler-service Specification

## Purpose
定义 macOS 当前用户 LaunchAgent 与 Windows LocalService 原生服务的无头同步契约：按每天或每周计划复用主应用原生爬取逻辑，使用固化的绝对路径访问凭据与数据库，记录审计，并处理注册、启停、注销及权限失败。

## Requirements

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

### Requirement: 复用既有爬取语义
定时任务 SHALL 复用手动同步的 C++/Qt runner、历史接口、ps=30、cursor、至少 1s 页间隔、认证/终止/审计与 SQLite 存储，不依赖脚本爬虫。

#### Scenario: 定时重复同步
- **WHEN** 定时同步后手动同步同一批历史
- **THEN** 已存在事件不被覆盖或重复，新的真实观看时间单独保存。

#### Scenario: 服务同步幂等
- **WHEN** 凌晨服务同步完成后，用户在主应用中手动再同步一次且云端无新记录
- **THEN** 手动同步新增为 0，数据库行数与服务同步结束时一致

#### Scenario: 服务运行中登录失效
- **WHEN** 服务爬取时接口返回 code=-101
- **THEN** 该次运行在审计中记为 failed，错误信息包含登录失效原因，爬取立即停止

### Requirement: 注册时固化路径
注册 SHALL 原子保存 executable/cookie/db/export/config 的绝对路径；无头入口 SHALL 仅使用这些配置，不按工作目录探测；Cookie 内容 MUST NOT 复制到计划描述或日志。

#### Scenario: 凭据失效
- **WHEN** 配置的凭据文件缺失或失效
- **THEN** 同步失败并在可写数据库留审计，退出码非零。

#### Scenario: 服务进程独立于主应用目录
- **WHEN** 服务安装在独立目录（其上级目录中没有 cookie 文件）且按注册配置运行
- **THEN** 服务能正确读取 cookie 并写入与主应用相同的数据库

### Requirement: 服务运行结果审计
定时同步 SHALL 写既有 sync_runs，source=scheduled，记录起止/页数/读取/新增/已存在/结果与错误，成功后原子导出；手动来源为 manual。

#### Scenario: 查询近期运行
- **WHEN** 用户打开服务窗口
- **THEN** 可查看最近手动和定时运行的真实状态及统计。

#### Scenario: 服务运行留痕
- **WHEN** 服务完成一次成功爬取
- **THEN** 审计表中新增一行来源为"定时服务"、状态为 success 的记录，导出 JSON 更新为最新数据

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

### Requirement: 与主应用并发访问安全
系统 SHALL 用同库跨进程锁阻止重叠爬取，并以 SQLite WAL、busy timeout 与页事务保持并发读取和写入完整性。

#### Scenario: 同步冲突
- **WHEN** 手动和定时同步重叠
- **THEN** 其中一个明确返回忙碌，已保存数据和页事务保持完整。

#### Scenario: 手动与定时同步重叠
- **WHEN** 用户在主应用点击"同步"的同时服务正在爬取落库
- **THEN** 两次同步均完整完成或一方明确失败，数据库无损坏、无部分写入的页

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
