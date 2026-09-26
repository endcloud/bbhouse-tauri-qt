# 项目维护索引

- 内存优化修复（2026-09-21）：[页面可靠性修复与手测](doc/内存优化修复与杜比用例验证.md)、[播放器换档与真实杜比实测](doc/播放器换档内存修复与杜比实测.md)。optimize-memory-footprint 补齐页面恢复、分页控件、动态首次追加序淘汰及裁剪锚点；reduce-player-memory-churn 复用同内容在途/已完成弹幕、增加默认关闭的白名单诊断。BV1omoHYzEST 在 Arc 140T 的 OpenGL d3d11va-copy 私有提交阶段末313→1679→313MiB，主要杜比台阶尚未消除；独立原生D3D11探针峰值约1210MiB不等于Qt集成完成。2026-09-23 以远端 `ExpiringPageLoader` 为基线变基，单活跃 Loader 被取代，超时回收同步清除搜索/页码/滚动记忆。两项change待用户手测，未归档；勿把原SUMMARY预测数字作为实测。新版mpv curl会在空代理时读取环境代理，媒体使用已显式直连的libavformat路径；[探针工具](tools/memory-review/README.md)。

- 当前品牌与版本：BBHouse 2.0.3，Windows 启动入口 BBHouse.exe；Qt/CMake 内部标识和用户数据路径保持兼容，见 [2.0.3 发布](doc/BBHouse2.0.3发布.md)。Windows Release 首帧闪退（SMTC 匿名命名空间接口被 GCC 优化为错误调用）已于 2026-09-19 用户确认 pass 并归档；发布必须保留真实视频和原生媒体回归门禁。

- 打包约定（2026-09-21 用户明确要求）：单纯版本更新和 Release 打包不需要 OpenSpec 介入，不创建、更新或归档 OpenSpec change；直接更新版本、构建验证、提交、打包并记录交付。实际功能变更仍使用 OpenSpec。

- 默认使用中文。实现按 `openspec/` 的 spec-driven 流程记录；UI/UX 由用户手测，代理负责构建、静态审查和非交互回归。
- 下载与本地媒体：[交付与手测](doc/下载管理与本地媒体库.md)、[主规格](openspec/specs/download-manager/spec.md)。`add-download-manager` 已于 2026-09-19 用户验收并同步归档；Windows 原生验证边界保留在交付文档。独立 downloads.sqlite；aria2 DASH、FFmpeg MP4/M4A、系统 curl XML/SRT，媒体直连且不持久化 Cookie/签名地址。本地起播等待当前 libmpv 渲染上下文就绪，不读 Cookie/写在线历史/发心跳；进度按 ID 更新卡片，勿每次替换列表。导入封面后台提取内嵌图片/首帧；移除默认只删记录，勾选才删关联文件并等待下载/封面进程结束，下载专属同名时间戳空目录随后清理，导入目录和含无关文件的目录保留。
- 先读 [项目状态与构建](README.md)、[最新修复交付](doc/运行修复与手测清单.md)。历史交付说明不代表当前所有规格已实现。
- 港澳台番剧与请求级代理：[交付与手测](doc/港澳台番剧与代理设置.md)。仅区域详情/PGC 取流 API 使用不可变代理快照；其他 API 和 libmpv 音视频显式直连，禁止全局设置代理。
- 视频卡片作者入口、个人空间及返回：[交付与手测](doc/个人空间与卡片作者导航.md)。空间控制器独立于特别关注，作者 UID 使用字符串跨 QML 边界。
- 特别关注当前 UP 名称与卡片作者：[修复与手测](doc/特别关注名称与卡片作者修复.md)。按条目作者 UID 复用本地资料补齐昵称，搜索与批量播放使用同一展示投影。名称右侧预留 24px 避让滚动条；`fix-special-follow-author-display` 已于 2026-09-18 用户验收并同步归档。
- 动态标题刷新、特别关注头像七天缓存与系统媒体控制：[交付与手测](doc/系统媒体控制与头像缓存.md)。
- 导航启动选中态、动态六档分类与视频默认值：[调整与手测](doc/导航初始选中与动态分类调整.md)，同时记录三个已验收修复的规格同步归档。
- 页面搜索按页隔离、视频主体点击播放、专栏空态与稍后再看分页：[修复与手测](doc/页面搜索与卡片交互分页修复.md)。搜索监听 FluTextBox 的 commit 信号；稍后再看先全量筛选再本地 30 条分页。
- 播放器控制面板结构与交互验收：[播放器控制面板重构与手测](doc/播放器控制面板重构与手测.md)。
- 标准播放器列表完全折叠、全屏联动及光标随面板隐藏：[交付与手测](doc/播放器列表与光标显隐调整.md)。底部列表按钮是唯一手动切换入口，已于 2026-09-18 用户验收并归档。
- 播放按钮初始状态、列表省略及标题栏按钮修复：[交付与手测](doc/播放状态与窗口布局修复.md)。
- 多集选集与封面加载优化：[定位与手测](doc/多集选集与封面加载优化.md)。已于 2026-09-18 用户测试 pass 并同步归档。选集行高度不得随隐藏归零，避免破坏 ListView 虚拟化；DASH audio-add 异步执行并隔离旧回调。横版卡片统一 400×225 WebP，番剧页竖版封面仅 `@.webp` 转格式；预览继续使用原图。
- macOS 硬解帧截图与全平台压缩：[定位与手测](doc/macOS帧截图修复.md)。macOS 使用 `hwdec=auto-copy`，Windows 保留 `auto-safe`。截图后台串行处理，最终严格小于 3 MiB；小 PNG 原样保留，大图转 JPEG 并按需降采样，临时原图清理，提示最终路径。macOS 截图及压缩已于 2026-09-19 用户测试 pass，主规格已同步并归档。
- 动态页分区补查闪烁：[增量模型修复与手测](doc/动态分区补查闪烁修复.md)。卡片使用稳定 Qt 列表模型，勿退回每次回应替换 QVariantList 的 Repeater。
- B 站 API 持久化检索索引：[doc/bilibili-api-index.md](doc/bilibili-api-index.md)。优先按域找到参考文档再改端点；aid/cid/mid 不得放入 QML 32 位 int。
- FluentUI 与页面审查：[doc/ui-layout-review.md](doc/ui-layout-review.md)；mpv/弹幕审查：[doc/player-runtime-review.md](doc/player-runtime-review.md)。
- 弹幕重构评估与性能基线：[bililocal 弹幕评估](doc/bililocal弹幕重构评估.md)；[场景图重构交付与手测](doc/弹幕场景图重构与手测.md)。
- 弹幕设置、密度与相似合并：[交付与手测](doc/弹幕显示设置与相似合并.md)；[pakku 算法与参考索引](doc/pakku弹幕合并参考.md)。两种实现共用设置，后台合并不在渲染帧执行。
- 弹幕实现选择与参考项目索引：[预缓存图像方案与手测](doc/弹幕实现切换与预缓存图像.md)。`sprite` 采用异步图片缓存与前向调度，2026-09-19 发布预设将其设为新用户默认，已保存的 `scene` 选择保留；用户于 2026-09-18 确认手测完全通过。
- OpenSpec 归档审计与早期规格差异：[积压清理记录](doc/OpenSpec积压清理与遗留差异.md)。已归档表示对应 change 范围完成，不代表原 WinUI 全量规格均已迁移。
- 直播导航与原生播放：[交付与手测](doc/直播页面与播放手测.md)、[接口与内核调研](doc/直播接口与内核调研.md)。`add-live-page` 已于 2026-09-18 用户手测 pass 并同步归档。直播使用独立控制器，房间/主播 ID 为字符串；API/媒体直连，无点播心跳、进度或 XML 弹幕。
- 区域番剧与个人空间已于 2026-09-18 用户手测 pass 并同步归档：[归档记录](doc/区域番剧与个人空间归档记录.md)。
- 流行页四档内容、每周必看历史选期与 PGC/音乐榜：[交付与手测](doc/流行页面与每周必看选期.md)。`add-popular-page` 已于 2026-09-18 用户验收并同步归档。期数来自服务端目录；PGC 榜单可仅含 seasonId，须经独立详情控制器进入整季播放；热门使用稳定列表增量追加，播放量不作观看次数。
- 本地历史追加保存、已记录 badge 与两平台定时服务：[交付与手测](doc/本地历史与原生定时服务.md)。macOS LaunchAgent / Windows Service + UAC（最新见下方迁移记录）；无头入口先于 GUI 初始化，路径固化、页间至少 1s、同库锁互斥；测试不得注册真实任务或修改用户库。两个 migrate change 已按用户本次要求重定范围，并于 2026-09-18 用户验收后同步归档；原待播/关窗归档仍未迁移。
- 本机 Qt：`/Users/ziyu/Qt/6.11.2/macos`；CMake：`/Users/ziyu/Qt/Tools/CMake/CMake.app/Contents/bin/cmake`。参考源实际位于 `/Users/ziyu/Documents/b3/`；`b3的替身` 是 Finder 别名，`b3` 是原 Windows 符号链接。
- 仅修改本项目内嵌 `3rd/FluentUI`，外部参考仓库只读；删除项目外文件或远程连接前必须询问用户。
- 不打印、提交 Cookie、带签名媒体地址或真实观看记录。测试临时文件放项目 build 内并清理，现有用户数据库保持原样。
- 提交前构建并运行 CTest；若用 Vitest，使用 `npx vitest <file> --reporter=dot`。任务复杂时可委派独立模块给子代理。

- 公开发布准备、默认值、CI 与许可：[发布评估](doc/发布准备与mpv分发评估.md)、[隐私审计](doc/发布隐私审计.md)、[第三方归属](THIRD_PARTY_NOTICES.md)。当前新用户默认 sprite/顶部1/4/合并开启/localhost:7890，保留旧配置；微软字体再分发授权按用户决定保留为正式发布阻塞项，未完成首次远程 tag CI 与 UI 手测。

- macOS 独立应用与拖拽 DMG：[打包与验证](doc/macOS应用与DMG打包.md)。脚本 `scripts/package-macos.py --clean-old` 附 Qt/libmpv/aria2c/FFmpeg/curl、重定位、arm64 裁剪、ad-hoc 签名和隔离部署冒烟，新包全部验证成功后清理旧版交付；内置 aria2 使用系统 CA，curl 使用 AppleSecTrust；本机媒体栈最低 macOS 27。`.app` Cookie 仅从用户数据目录读取，开发裸程序仍查仓库根；不将真实凭据复制入包。

- 媒体 CDN 优先级：[修复与手测](doc/Windows任务注册与CDN优先级修复.md)。媒体共用云 CDN/普通节点/P2P 排序，DASH 音轨及 durl 保留完整候选；`fix-scheduler-registration-and-cdn-priority` 已于 2026-09-19 用户确认 Windows 手测 pass 并同步归档。该文 Windows XML 修复已由下列服务方案替代。
- Windows 原生服务与 UAC：[迁移与手测](doc/Windows原生服务与UAC.md)。删除 schtasks/XML 后端，原生无 Qt 依赖宿主运行 LocalService + service SID；管理操作 runas，查询免提权。常驻 worker 先于 GUI 分流、固定运行时目录、日/周计划持久防重、停止事件取消；macOS LaunchAgent 保留。`replace-windows-scheduler-with-service` 已于 2026-09-19 用户确认 Windows 手测 pass 并同步归档；代理测试仍不得安装真实服务或修改用户 ACL/数据库。


- 下载交互与清理（2026-09-19）：弹窗单次清晰度；视频含音轨，与仅音频互斥。用户已明确授权下载成功清理本任务临时分轨/标记（包括用户下载目录），失败取消保留续传文件；禁止扩大为清空目录或删除正式媒体/导入文件。见下载交付文档。

- 1.0.1 登录、关于与 CC 字幕：[交付与手测](doc/登录初始化与关于及CC字幕.md)。首次无格式有效 Cookie 显示独立登录窗口；扫码入口因用户测试反馈暂时隐藏，文本或文件导入经 nav 验证后原子保存到当前 Cookie 读取路径，失败保留旧凭据；设置末尾重新登录。关于独立导航，仓库为 endcloud/bbhouse-tauri-qt，逐项展示依赖与参考。标准播放器 CC 位于弹幕左侧，在线含 AI、本地内嵌/旁挂均可选；新内容默认关闭且不持久化，字幕异步结果隔离。应用和 macOS 包版本统一来自 CMake。[Cookie-Editor 导入帮助](doc/Cookie导入帮助.md) 同步于登录页，完整 Markdown 嵌入 `:/help/Cookie导入帮助.md` 随包分发；底部导航从上到下为设置、关于。该 change 已于 2026-09-19 用户确认并同步主规格归档；扫码仍隐藏，Windows 原生验证边界保留。

- 全平台应用图标：[导入与手测](doc/全平台应用图标.md)。素材位于 `app/resources/icons/`；Qt/FluentUI/关于页使用内嵌 PNG，Windows 主程序与服务宿主编入 ICO，macOS 打包 ICNS 并校验，PNG/ICNS 主体统一为 824/1024 居中留白，原图保留且由 `scripts/generate-macos-icon.py` 生成；Linux DesktopIntegration 组件安装 desktop/hicolor 资源。`add-platform-app-icons` 已于 2026-09-19 用户确认图标及 Dock 修复通过并同步归档；外部素材只读。

- public 发布流程与 GitHub 认证：[发布与归档](doc/Qt源码发布与Tauri归档.md)。`migrate-public-repository` 已用户验收并同步归档。开发库与 `/Users/ziyu/Documents/code_g/bbhouse-tauri-qt` 是独立历史；后续按已授权范围白名单同步，保留 public README，排除原 .git/凭据/用户数据/构建产物/b3 链接；构建、CTest、隐私与规格校验后普通推送 master，核对远端 SHA，tauri 固定保留 d22550e。不得重复初始化或默认强推。Git 使用本机 SSH 认证为 endcloud；普通推送可能使用 Ruleset bypass，成功不等于已满足签名/PR 规则。文档维护不顺带发布开发新版本，tag/Release 另行授权。
- public 2.0.3 同步与隐私复核（2026-09-26）：[交付记录](doc/public源码同步2.0.3.md)。白名单继续排除 CodeGraph 索引及两个旧编译库 `3rd/FluentUI/FluentUI/fluentuipluginplugin.lib` / `libfluentuipluginplugin.a`；完整 OpenSpec 归档同步，public README 保留。此次用户授权清理 public 的两个库与四个已归档图标旧记录，不扩大为今后项目外文件自动删除。

- 应用名称与版本：[名称统一（2.0.2 起）](doc/BBHouse名称与版本更新.md)，当前 2.0.3。全局显示名统一 BBHouse，版本从 CMake 读取；内部 bbhouse-qt 标识、数据路径、可执行文件名与 bundle ID 保持兼容，避免已有凭据/历史失联。macOS 新包为 BBHouse.app，Windows 产品资源与发行目录同步品牌。`rename-app-bbhouse-2-0-2` 已于 2026-09-19 用户确认并同步归档，发行包待后续重建。

- 内存与页面生命周期（2026-09-20）：[交付与手测](doc/内存生命周期与后台页面回收.md)、[指针所有权审计](doc/指针与资源所有权审计.md)。后台页面默认 5 分钟后卸载，设置可选 1/5/10/30 分钟；八类浏览控制器清理缓存并用 generation 拒绝旧结果。后台下载/同步/独立播放不随页面销毁。UP/榜单/番剧详情各保留最多 12 份缓存，头像内存元数据 512 项；QObject 父所有权、RAII、自有线程池析构等待和 watcher 回投必须保留。`optimize-memory-lifecycle` 待用户 UI/Windows 手测，未归档。
