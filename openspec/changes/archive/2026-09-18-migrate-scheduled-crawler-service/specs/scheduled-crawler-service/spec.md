## MODIFIED Requirements

### Requirement: 每日定时触发
系统 SHALL 支持每天或每周指定星期，在本地 HH:mm（默认每天 01:00）由平台原生任务触发。macOS 使用当前用户 LaunchAgent，Windows 使用当前用户 Task Scheduler；要求登录会话，主应用可关闭。Windows 不补跑错过时刻，macOS 休眠唤醒遵循 launchd 合并触发规则，并在界面说明。

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
系统 SHALL 以当前用户最低权限注册、查询、修改、暂停/启用与注销原生任务；不默认要求管理员。权限/策略拒绝、超时或取消 SHALL 明确报错且查询失败不得当成未注册。注册/更新失败 SHALL 恢复旧配置和任务；回滚失败须明确报告实际不一致状态。注销保留数据与配置。

#### Scenario: 权限拒绝
- **WHEN** 平台拒绝注册或修改任务
- **THEN** 显示权限错误，不宣称成功，保留可恢复配置并重新核对状态。

#### Scenario: 查询注册状态免提权
- **WHEN** 主应用以普通权限运行并检测服务注册状态
- **THEN** 检测完成，全程无 UAC 弹窗

#### Scenario: 拒绝提权不残留
- **WHEN** 操作被权限或系统策略拒绝（当前用户任务默认不发起提权）
- **THEN** 报告拒绝，恢复此前配置和注册状态；回滚失败单独明确显示。

### Requirement: 与主应用并发访问安全
系统 SHALL 用同库跨进程锁阻止重叠爬取，并以 SQLite WAL、busy timeout 与页事务保持并发读取和写入完整性。

#### Scenario: 同步冲突
- **WHEN** 手动和定时同步重叠
- **THEN** 其中一个明确返回忙碌，已保存数据和页事务保持完整。

#### Scenario: 手动与定时同步重叠
- **WHEN** 用户在主应用点击"同步"的同时服务正在爬取落库
- **THEN** 两次同步均完整完成或一方明确失败，数据库无损坏、无部分写入的页
