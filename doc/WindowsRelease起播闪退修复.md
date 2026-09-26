# Windows Release 起播闪退修复（2026-09-19）

OpenSpec：`fix-windows-release-playback-crash`。

## 问题与根因

用户反馈 `D:\release\bbhouse-qt[af874cc]` 在线、本地视频首帧出现后卡死退出，同段视频 Debug 正常。Windows Application 事件 1000/1001 记录该包 `0xc0000005` 执行访问冲突。

根因在 `app/player/SystemMediaBackend_win.cpp`：WinRT COM 抽象接口 `MediaAbi` 被包在匿名命名空间中，具有内部链接。GCC 13.1 的 Release `-O3` 将无法在当前翻译单元找到实现的虚调用去虚化为 `__cxa_pure_virtual`，而实际实现来自 Windows DLL。原 EXE 中 `WindowsMediaBackend::update` 的 `0x14016bc5f` 是 `call 0x100000000`，对应对象文件含 pure-virtual 重定位；系统媒体挂接在首帧后发布播放状态时触发。Debug 未进行同样优化。

用相同编译器、真实隐藏 HWND 和原生 SMTC 的隔离小程序复现：旧代码挂接后退出 `-1073741819`（`0xc0000005`）；仅把接口放到外部可见的具名命名空间后退出 0。具体实现类仍留在匿名命名空间，保留全局 Release 优化。

## mpv 与打包审查

- `3rd/mpv`、`build/bin`、旧包三份 `libmpv-2.dll` SHA-256 相同：`7469d0493ed908ca4d3ea46681b930ccc29a159f430e4375a0305bba94f3deba`。本轮不替换内核、不关闭硬解。
- 打包 strip 仅处理应用、服务宿主及 FluentUI 暂存副本，未处理 mpv；Qt/MinGW 对应运行库与 SDK 文件一致。崩溃也存在于未裁剪 EXE。
- 上轮 PCM/`vo=null`/`hwdec=no` 冒烟只能确认音频解码，遗漏视频渲染和起播后的系统媒体调用。
- `scripts/package.sh` 改为转发同一 PowerShell 发布流程，移除第二套过时裁剪规则；不能再使用会删除 FluentUI 所需 labs 模块的旧规则。
- Tag CI 仍是 build-only；无原生桌面/内核的环境明确排除对应测试。完整发布必须在 Windows 原生桌面执行发布脚本，通过裁剪后的实际包验证。

## 回归门禁

- `system-media-windows` 在 MinGW 下显式 `-O3`，覆盖真实 HWND 挂接、状态/标题/倍速/时间线读取、移除事件及重新挂接；部署 EXE 复用同一验证入口。
- `local-player-windows` 在不可见 OpenGL surface 上运行真实 `PlayerController`/`MpvVideoItem`，检查 H.264 红色视频像素、渲染就绪、快速切换及过期通知隔离。跨平台路径比较使用规范路径，重挂载恢复 QML 对应的 visible 状态。
- 发布包内 `--deployment-smoke-test` 新增：应用本身的 OpenGL 渲染、视频进度超过 0.5 秒、真实 Windows 系统媒体控制、关闭重开两次。H.264 合成样本仅 2872 字节内嵌，无外部视频或用户数据。
- 原有音频、SQLite、图片、QML、PE 导入闭包和 FFmpeg/aria2/curl 检查继续执行。视频/原生媒体不成功、超时或缺少完成标记均阻止发布。

验证数据位于项目 build 内；不读取真实 Cookie/数据库、不访问账号 API、不安装服务、不修改其他软件或驱动。用户仍需复测在线内容、声音、全屏和系统媒体键的真实交互。

## 交付记录

- Release 主程序和全部 `regression-tests` 构建成功；全量 CTest **37/41 通过**。新增 `system-media-windows`、`local-player-windows` 均通过，`local-player`、`live-player`、`screenshot`、系统媒体控制逻辑等也通过。
- 四项失败与之前 Debug 已记录的问题同类：`player-runtime` 的代理/异步 DASH 音轨断言、`preferences` 的代理配置断言、`popular-page` 的无 QML 警告断言、`history-controller` 的 `Parameter count mismatch`。未在本轮扩大修复；Release 的 `danmaku-scene` 本轮通过，不把这视作 Debug QObject 断言已经修复。
- 修复后的对象文件没有 `REL32 __cxa_pure_virtual` 调用重定位（抽象类 vtable 仍可包含正常的 pure-virtual 数据项）。
- PowerShell/Bash 语法、OpenSpec 严格验证、diff 检查通过。精简暂存包的真实视频、依赖和外部工具检查通过。

修复源码提交：`7b03267`。最终包：`D:\release\bbhouse-qt[7b03267]`，**232 个文件、310164834 字节（295.80 MiB）**。最终路径再次通过真实视频/SMTC、57 个 PE 文件依赖闭包、FFmpeg/aria2/curl 验证；231 条 SHA-256 全量核验通过。旧包保留；后续提交仅补充交付记录，不改变本包代码。

用户于 2026-09-19 明确反馈“测试完成，pass”，本轮起播修复已验收。后续 BBHouse 2.0.2 继续保留该修复和原生验证门禁。

清理边界：自动审批以 `blocked by policy` 拒绝删除已核验位于项目内的 `build/release-stage`、`build/smtc-regression` 和 `build/app/history-controller-rzJAnX`。这些暂存/测试目录及本轮 build 日志保留，不影响发布包；未改用其他通道绕过删除限制。

用户随后明确授权清理，但自动审批仍拒绝同一删除操作；清理事项移交 `brand-bbhouse-2-0-2` 记录，验收不代表临时目录已删除。

后续已完成旧临时目录清理：取消强制删除后，普通删除通过自动审批。详见 [2.0.2 发布记录](BBHouse2.0.2发布.md)。
