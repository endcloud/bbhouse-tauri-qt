## Decisions

- 爬虫完全复用 Qt Network/Qt Sql/C++，工作线程或 QCoreApplication 无头入口运行。等待使用可取消的累计至少 1000ms 间隔，错误不自动高速重试。
- 每天/每周与本地 HH:mm 映射为 launchd StartCalendarInterval、Windows CalendarTrigger；系统时区/DST 由原生调度器处理。Windows StartWhenAvailable=false；launchd 休眠唤醒可能合并补发一次，按平台真实行为文案说明，不承诺跨平台绝对不补跑。
- LaunchAgent 属当前 GUI 用户域；Windows 采用 InteractiveToken/LeastPrivilege 的当前用户任务，不使用 SYSTEM 身份或默认 UAC。被 TCC、企业策略、文件权限拒绝时保留错误；应用不尝试绕过。
- 注册标识按账户/数据目录稳定隔离；固定绝对路径配置，不依赖服务工作目录或自动搜寻 Cookie。移动程序或凭据后需重新保存/注册。
- 系统状态为事实来源，查询失败不等于未注册。修改任务/配置时保留旧配置并尝试回滚，回滚失败须明确告知并重新查询实际状态；注销保留本地数据库、导出与配置。
- 手动/定时共用库路径锁，重叠时明确“同步进行中”，不会双进程同时爬取；SQLite 页事务仍负责数据完整性。
- 独立窗口使用 Qt 逻辑尺寸与 FluentUI 主题，异步系统操作期间禁用冲突按钮，错误可见，日志仅展示统计/时间/来源/错误，不显示 Cookie 或原始历史响应。

## Validation

单测覆盖 daily/weekly 序列化、特殊路径转义、权限拒绝/取消/超时/不存在/回滚、无头参数与审计；使用 build 临时目录/伪调度命令，不操作真实系统注册和用户数据库。macOS 构建/离线回归；Windows 代码与任务 XML 静态审查，真实注册、休眠、权限弹窗与 Windows 运行留给用户验收。
