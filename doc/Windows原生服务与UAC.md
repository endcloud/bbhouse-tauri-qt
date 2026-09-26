# Windows 原生服务与 UAC

日期：2026-09-19。OpenSpec：`replace-windows-scheduler-with-service`。按用户明确要求取消 Windows 任务计划后端，改为 Windows Service + UAC；默认 LocalService 方案已由用户确认。用户于 2026-09-19 确认 apply 完成且 Windows 手测 pass，已同步主规格并[归档](../openspec/changes/archive/2026-09-19-replace-windows-scheduler-with-service/)。

## 替换范围

此前任务 XML 先出现文件占用，修复句柄交付后用户仍报告 `(1,40) 无法切换编码`。本次不再补 XML 编码：删除 `HistoryScheduler` 中 Windows 任务 XML 生成、schtasks 查询/安装/卸载、whoami 与 XML 回滚实现。

- Windows 改用 SCM 原生服务，查询普通权限；注册、保存计划、暂停/启用、注销经 ShellExecuteExW `runas` 请求 UAC。主 GUI 不要求管理员启动。
- UAC 取消返回明确提示，不发布配置、不改变服务，也不为回滚再次请求 UAC。
- macOS LaunchAgent、历史数据库和同步语义保持；上次媒体 CDN 优先级与回退改动保留。
- 原 Windows XML 增量规格已从旧 change 移除，旧说明加“已替代”标识，避免后续归档再次引入 schtasks。

## 服务与运行身份

新增 `bbhouse-history-service.exe` 是独立、无 Qt DLL 依赖的原生 WinAPI 程序，构建主程序时自动一起构建，并加入 Windows deploy 和 CI 编译产物。MinGW 静态链接 C++/线程运行库；MSVC 使用静态 CRT。不要只复制 `bbhouse-qt.exe`，需要将宿主放在同目录。

宿主实现 `StartServiceCtrlDispatcherW`、`ServiceMain`、SCM 状态报告与 STOP/SHUTDOWN 处理。服务显示名为 `BBHouse History (<hash>)`，内部名称沿用本机用户与配置路径派生的 `com.bbhouse.history.<hash>`，不同配置隔离。

服务使用 `NT AUTHORITY\LocalService`，配合独立 `NT SERVICE\<服务名>` SID：

| 资源 | 授予服务 SID 的访问 |
| --- | --- |
| 宿主、应用及必要 Qt/编译器运行库和插件目录 | 读取/执行 |
| Cookie 文件 | 显式只读，避免同数据目录的写权限继承到该文件 |
| 配置、SQLite/锁/WAL、导出及运行状态所在目录 | 创建/读写/清理必要数据文件 |

不授予磁盘根目录或 Windows 系统目录递归权限，不以 LocalSystem 运行用户可修改的应用。服务只使用注册时固定的本地绝对路径；不能依赖映射盘、交互会话或 Qt Creator 仍在运行。文件/目录需要存在且可授权；替换 Cookie、移动应用或升级 Qt 后可重新保存计划以刷新路径与权限。卸载保留配置和数据，既有 service SID 文件 ACL 不主动重写，注销后不再有该服务的运行令牌。

## Debug 运行库与启动握手

GUI 收集实际 Qt/编译器运行库路径，显式传给原生宿主；宿主组装 worker 的 PATH 并加入系统 PATH，而非依赖 Qt Creator 的父进程环境。应用/插件目录权限一并准备。

服务启动同目录关联的主应用：

```text
bbhouse-qt --history-service-worker --config <绝对路径> --stop-handle <继承事件> --ready-handle <继承事件>
```

入口在 QApplication/QML/mpv/偏好和页面控制器前分流，仅创建 QCoreApplication。worker 成功检查配置、持久状态及互斥锁后发送 ready，宿主才向 SCM 报 RUNNING；缺 DLL、坏配置或重复进程不会仅因 CreateProcess 成功而误报注册成功。worker 放入 kill-on-close Job，宿主异常退出不会遗留孤儿爬虫。

Qt Creator Debug 更新二进制前请先在服务管理中暂停，避免常驻进程占用 exe/DLL；编译后启用。正式长期运行仍推荐完整 Release 部署目录。

## Windows 构建占用排查

2026-09-19 在 Windows / Qt 6.11.1 / MinGW 13.1.0 的原 Qt Creator Debug 目录复现：

```text
ld.exe: cannot open output file ..\bin\bbhouse-history-service.exe: Permission denied
collect2.exe: error: ld returned 1 exit status
mingw32-make.exe: *** [app\CMakeFiles\bbhouse-history-service.dir\build.make:106: bin/bbhouse-history-service.exe] Error 1
```

`Error 1` 是 make 汇总信息，应查看它前面的链接器错误。本次 SCM 的运行中服务宿主路径与失败输出完全一致，服务还持有同目录的 `bbhouse-qt.exe` worker，导致 Windows 不允许链接器覆盖 EXE；没有发现对应的 C++ 或链接参数缺陷。

恢复步骤：

1. 在应用的本地历史服务管理中暂停服务并确认 Windows UAC，等待状态显示已停止。正常停止会通知 worker 取消并收尾，可能需要几十秒。
2. 完全退出 GUI，再构建原 Qt Creator kit。关闭 GUI 本身不能停止 Session 0 的服务和 worker。
3. 构建成功后按需重新启用服务。长期后台运行宜使用独立 Release 部署目录，以免再次占用开发输出。

可在 PowerShell 7 中只读检查服务状态，并显示链接器完整错误：

```powershell
Get-CimInstance Win32_Service |
    Where-Object { $_.Name -like 'com.bbhouse.history.*' } |
    Select-Object Name, State, ProcessId, PathName |
    Format-List

& C:/Qt/Tools/CMake_64/bin/cmake.exe --build `
    build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug `
    --target bbhouse-history-service --verbose
```

不要把 `taskkill /F`、删除 EXE、卸载服务或更改文件 ACL 作为此故障的常规处理。若服务已停止仍失败，应检查 GUI 或残留进程是否使用同一输出目录，再根据新的首个错误处理。

解除占用后发现两个独立问题：

- C 盘仅剩约 10 MB，编译器写临时汇编文件时报 `No space left on device`；用户释放空间后继续构建，代理未删除项目外文件。
- MinGW Makefiles 报 `No rule to make target .../doc/Cookie导入帮助.md`，该文件实际存在。`app/CMakeLists.txt` 现通过 `configure_file(COPYONLY)` 暂存为构建目录中的 `help/cookie-import-help.md`，RCC 依赖使用英文路径；`QT_RESOURCE_ALIAS` 保留 `:/help/Cookie导入帮助.md`。原文档仍是唯一来源，修改后自动重新配置并更新资源。原文档与暂存文件 SHA-256 一致，生成的 make 依赖与 QRC 别名均已核对。

本次 Windows 原生复验（2026-09-19）：

- 原 Qt Creator `Desktop_Qt_6_11_1_MinGW_64_bit_Debug` / MinGW Makefiles 的 `bbhouse-qt`、`bbhouse-history-service`、`regression-tests` 全部构建成功，退出码 0。
- 完整 CTest **34/39** 通过；`history-scheduler`、`history-sync`、`history-service-entry`、`history-service-worker` 以及登录和关于页面回归通过。
- 以下 5 项单独串行复跑仍失败，本次没有修改这些运行时模块，不能将构建恢复表述为全量回归通过：

| 测试 | Windows 本机失败现象 |
|---|---|
| `player-runtime` | 本地 HTTP 媒体、代理隔离及异步 DASH 音轨 fixture 多项断言失败；本地音频和 mpv 初始化通过 |
| `danmaku-scene` | 收尾触发 `DanmakuEngine` QObject 类型/析构期断言，退出码 `0xc0000602` |
| `preferences` | 代理配置保存、鉴权与快照相关断言失败 |
| `popular-page` | 页面运行警告检查失败；其余已输出的页面断言通过，并有离屏字体目录警告 |
| `history-controller` | 初始化失败后重试断言失败，随后 `start sync run: Parameter count mismatch` 异常终止 |

测试使用项目 build 内隔离数据，未注册服务、改用户 ACL 或真实用户库。编译后服务仍保持停止，可由用户按需重新启用。OpenSpec 记录为 `fix-windows-service-build-lock`。

## 计划、停止与恢复

- worker 每 500 ms 检查已保存的每日/每周 HH:mm，目标分钟执行，错过不补跑、不唤醒系统。关闭 GUI、退出登录后服务仍可运行；服务启用时配置为自动启动，暂停时停止并设为禁用。
- 配置旁 `history-service-state.json` 按计划指纹持久保存最高已尝试日期，先保存再同步，重启/DST 回拨/同分钟轮询不重复。修改计划允许新计划生效，改回旧计划仍保留之前尝试记录；失败不在同日计划内循环重试。
- 复用 HistorySyncRunner、至少 1 秒页间隔、同库跨进程锁、SQLite 事务、scheduled 审计与导出。不读取服务账号自己的默认数据库。
- STOP 通过仅继承给 worker 的事件设置取消标志。独立线程监听避免网络请求的嵌套 Qt 事件循环阻塞取消；等待当前请求收尾（API 超时为 30 秒），最多给予 45 秒，SCM 管理端停止等待为 60 秒；仅超时兜底强制停止，并报告非零结果。
- 提权前仅写临时请求。接受 UAC 后，helper 停止旧 worker，原子发布配置、设置必要 ACL 和 SCM，再启动握手。失败恢复旧配置、ACL、注册/启动类型及原运行状态；恢复失败以独立错误码 `536870913` 告知“回滚失败，请刷新并检查实际服务状态”。
- 新服务配置成功后通过本机 Task Scheduler COM 仅移除同名旧任务，避免两套机制重复触发；没有旧任务视为成功。这只是旧版迁移清理，不生成任务 XML，也不调用 schtasks。旧任务清理失败会撤回本次服务变更。

## 自动验证与边界

- macOS Qt 6.11.2 Release 主程序与全部测试、Qt Creator Debug 主程序构建通过；完整 CTest **33/33** 通过；最终补修后服务相关 3/3 定向 CTest 再次通过。
- `history-scheduler` 使用注入 helper，验证查询无提权操作、坏状态/访问拒绝区分、UAC 取消不改配置或二次提权、保存/启停/注销、回滚失败诊断和请求临时文件清理；macOS 原有测试保留。
- `history-service-worker` 用注入同步函数、真实临时配置与状态文件验证日/周触发、无补跑、DST/回拨/重启防重、失败后下次计划、损坏配置/状态、持久化失败和重入/停止。Windows 专属分支额外测试真实子进程 ready/stop 事件、坏句柄与坏配置；此分支需 Windows CTest 执行，本机没有运行。
- `history-service-entry` 继续验证旧一次性入口（macOS 使用）不会初始化 GUI 或访问默认用户库。
- 原生宿主用本机 MinGW Windows x64 工具链进行严格语法、完整静态运行库链接和 PE 导入检查，未依赖 Qt DLL 或 MinGW DLL。
- 两个受影响 OpenSpec change 的 strict 校验、diff 检查通过；测试未弹真实 UAC、安装真实服务、改真实 ACL/Cookie/数据库。临时数据和交叉产物均在项目 build 内清理。

以上自动验证来自代理的 macOS 环境与交叉编译；Windows 手测通过结论来自用户于 2026-09-19 的明确反馈，未将交叉编译替代为原生验收。主规格已同步并归档，以下清单保留用于后续回归。

## Windows 手测与后续回归

1. 用普通权限 Qt Creator 构建并运行，确认 exe 同目录已有 `bbhouse-history-service.exe`。打开本地历史服务管理，注册确认后取消 UAC，应提示取消，配置和系统服务均不变。
2. 再次注册并接受 UAC，服务窗口显示已注册/启用。在 `services.msc` 查看 `BBHouse History`：登录账号 Local Service、自动启动、运行中；旧同名任务应已移除。
3. 保存日/周计划、暂停和启用，分别取消/接受 UAC，检查真实状态与 GUI 一致；查询/刷新不应弹 UAC。将计划设为几分钟后，关闭 GUI 和 Qt Creator，确认有 scheduled 审计与历史更新。
4. 保持服务启用，退出登录或重启后再次检查计划执行；错过关机/休眠时刻不补跑。遇到数据库/Cookie 权限或 DLL 错误，记录错误码与服务状态，不附 Cookie 或真实观看内容。
5. 同步过程中暂停，确认能在当前请求结束后取消且无孤儿进程；手动同步与后台同步重叠应由同库锁互斥。
6. 注销先取消 UAC，再接受，确认服务移除，SQLite/导出/配置保留，手动同步照常可用。程序移动或 Qt 升级后，先暂停、更新目录，再重新保存计划并启用。

## 验收与归档（2026-09-19）

- 用户确认 `replace-windows-scheduler-with-service` 已完成 apply，并在 Windows 上手测 pass，授权同步归档。
- 已同步 `scheduled-crawler-service`、`service-control-ui` 主规格，明确 SCM/UAC、LocalService/service SID、固定运行时、常驻 worker、计划防重、取消和卸载保留数据。
- 本轮仅更新规格与维护记录，未操作真实系统服务、ACL、凭据或数据库。
- 归档后代理复核：macOS 应用与回归目标构建通过，CTest **41/41**、OpenSpec 全量严格校验 **29/29**、差异检查通过；Windows 验收依据为上述用户反馈。
