## Why

原计划仅覆盖 Windows SCM 常驻服务和每日时间，不能满足当前 macOS/Windows、时间/周期管理以及最小权限需求。将它更新为系统原生的当前用户定时任务，复用 C++/Qt 爬虫保存本地 SQLite。

## What Changes

- 同一可执行文件增加无头 --history-sync-once --config <绝对路径> 入口，在 QApplication/mpv/QML 前分流至 QCoreApplication。
- macOS 使用 launchd LaunchAgent，Windows 使用 Task Scheduler 当前用户交互令牌、最低权限；每天或每周指定星期与 HH:mm（默认每天 01:00）。需要用户已登录，主窗口可关闭，不承诺注销/关机时执行。
- 注册固化 executable/cookie/db/export/config 的绝对路径，配置原子写入，系统参数通过结构化 XML/plist 和 QProcess 参数列表传递，禁止 shell 拼接。
- 同步接口使用 ps=30/type=all、服务端 cursor 翻页，页间至少 1s；空 data/list 正常结束，重复/循环或缺失有效游标明确结束或失败，不持续轮询；失败记录审计。
- 独立服务管理窗口显示注册/启用状态、时间、周期和最近运行；提供注册、保存、启用/暂停、确认注销；系统权限、策略禁用、超时及配置错误明确呈现，不假报成功。

## Impact

替换 scheduled-crawler-service 和 service-control-ui 的仅 Windows SCM/UAC 契约；不安装第三方爬虫/脚本服务，不复制 Cookie 内容，保留数据库和导出。平台注册/注销仅由用户在界面明确操作时执行；开发验证用替身命令与隔离文件，不注册开发机器实际后台任务。
