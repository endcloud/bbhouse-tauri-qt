# 1.0.1 登录初始化、关于页与 CC 字幕

用户于 2026-09-19 确认当前交付并要求同步归档。OpenSpec 已同步主规格并归档至 [2026-09-19-add-login-about-and-cc-subtitles](../openspec/changes/archive/2026-09-19-add-login-about-and-cc-subtitles/)。扫码入口保持隐藏；Windows 原生行为仍保留平台验证边界。

## 登录入口

- 首次未找到格式有效的主 Cookie 时显示独立初始化窗口；已有凭据保持正常启动。启动仅作本地格式判断，已过期但格式正常的 Cookie 可从设置末尾“重新登录”更新。
- 根据 2026-09-19 用户扫码测试反馈，暂时隐藏全部扫码入口及相关提示；控制器实现保留，界面不再触发二维码生成或轮询。当前仅提供 Cookie 文件/文本导入。
- 登录页提供可展开的 Cookie-Editor 导入帮助和 Edge 扩展商店链接；完整步骤见 [Cookie 导入帮助](Cookie导入帮助.md)，该 Markdown 直接嵌入 `:/help/Cookie导入帮助.md`，随应用分发。
- 支持选择文本文件或粘贴 Cookie 请求头，需包含非空 SESSDATA；支持 `Cookie:` 前缀与逐行 key=value。界面遮挡输入，提交后清空。不是浏览器整份 Cookie 数据库/JSON 导出导入器。
- 导入验证 nav 登录状态；缺少 DedeUserID 时补入服务端 UID，账号不一致拒绝。原子保存为当前 `AppPaths::cookiePath()` 下的 `bilibili.cookie.txt`，失败不覆盖原文件。凭据不进入日志/版本库，macOS 文件权限仅当前用户读写。
- macOS `.app` 保存到用户数据目录；开发程序沿既有最多四层父目录寻找 Cookie，否则写入可执行文件同目录。页面显示实际路径。不会复制开发机凭据到安装包。
- “稍后登录”进入下载管理，可使用本地媒体；成功登录进入动态。已有主窗口重新登录成功会刷新动态；其他已缓存的在线页面可使用原刷新入口更新。本地历史与特别关注配置不因重新登录清空。
- Windows 已安装原生历史服务时，更换 Cookie 后按[服务文档](Windows原生服务与UAC.md)重新保存计划以配置访问权限；本次不注册服务或修改真实 ACL。

参考：Tauri `src/components/Login.vue`、`src/stores/login.ts`、`src/bili_api/login.ts`，API 文档 `login/login_action/QR.md`、`login/login_info.md`。

## 关于与版本

- 导航底部从上到下为“设置”、“关于”，设置移除旧 About 卡片。
- 显示应用信息、版本 1.0.1、可复制版本、仓库链接与法律声明。
- 仓库链接统一为 https://github.com/endcloud/bbhouse-tauri-qt。
- 根据 `THIRD_PARTY_NOTICES.md` 将 Qt、FluentUI、libqrencode、QHotkey、QCustomPlot、Chart.js、ChartJs2QML、color 系列、mpv、FFmpeg、aria2、curl、SQLite、zlib，以及 wiliwili、bililocal、pakku.js、Danmaku、DanmakuFrostMaster、bilibili-API-collect 分别列项，含一句话介绍、许可与来源入口。
- 原有第三方许可和字体授权边界继续保留。
- 应用版本由 CMake 项目版本注入，macOS Info.plist、使用说明和 DMG 文件名从同一声明读取。本次生成开发构建，不重新制作 DMG 或发布 tag。

## CC 字幕

- 标准播放器右下角弹幕按钮左侧增加 CC 图标，菜单列出“关闭字幕”和全部可用轨道；无字幕显示空态。S 键可关闭或打开首个轨道。
- 每个新视频、切集、本地文件或新播放会话始终默认关闭，不保存字幕偏好。同内容清晰度/编码换源保留当前会话选择。
- 在线按播放器信息接口读取人工、AI 及自动生成字幕，保留 AI 标识；字幕 JSON 仅驻留内存并按播放进度显示。拖动、暂停、倍速和重叠字幕由时间轴处理，纯文本渲染。
- 本地列出内嵌字幕、下载记录关联字幕与同名/语言后缀旁挂 SRT、ASS、SSA、VTT、SUB；通过 mpv 选择。已下载 SRT 不再自动开启。
- 关闭、改变选择、切播及关窗使旧请求无效；字幕错误不阻断音视频。区域播放的字幕请求仍直连，不引入全局代理。

## 验证

- Qt 6.11.2 macOS Release 主程序及全部回归目标构建。
- 离线登录 Cookie 解析、字段编码、域过滤、原子替换、失败保留与私有权限。
- 真实 LoginPage/Controller 配合固定 nav 响应：无凭据初始状态、无效输入、无效登录保留原文件、账号匹配、补齐长 UID、成功通知、关闭清理。
- 关于与登录页在窄/宽尺寸下离屏装载，检查 QML 绑定；不进行 UI 自动化。
- 字幕 API fixture 含人工、AI、非法 URL；字幕时间轴覆盖排序、重叠与跳转。
- 真实 libmpv 本地字幕回归：SRT 选择和关闭、默认标记内嵌轨禁用、切播/关窗不记忆、旧回调不能重启字幕；不读取 Cookie、不写在线历史。
- 最终构建、CTest 和 OpenSpec 结果见本文末尾。

## 验收与后续平台复测清单

1. 使用无 Cookie 的新环境启动，确认初始化窗口仅提供文件/粘贴 Cookie 导入，无扫码入口；选择稍后登录后进入本地媒体库。已有 Cookie 的环境直接正常启动。
2. 展开导入帮助，按说明安装 Cookie-Editor，登录网页后导出 Header String；检查扩展商店链接、中英文及窄窗文字换行。
3. 导入有效文本文件和粘贴 Cookie；错误/失效文本不能覆盖旧凭据，路径不可写时提示错误。随后从设置末尾重新登录并刷新在线内容。
4. 查看关于页：中英文、明暗主题、窄窗口和展开导航，确认标题冻结、项目介绍换行、链接与版本复制正确。
5. 在线普通视频和番剧：有人工/AI/多语言/无字幕时菜单正确；选择、关闭、S 键、拖动、倍速、换清晰度、切集与关窗重开行为符合上文。
6. 本地媒体：内嵌默认字幕仍默认关闭；下载 SRT 与语言后缀旁挂能逐一选择；关闭/快速切播不残留旧字幕。字幕、弹幕与控制面板布局不互相遮挡。
7. Windows 额外确认文件选择器、Cookie 写入权限、本地字幕与原生媒体播放。不要把真实 Cookie、二维码密钥或带签名的媒体链接附到反馈中。

## 最终检查结果

- macOS Qt 6.11.2 Release 主程序与 `regression-tests` 构建通过。
- CTest **41/41** 通过（含真实 mpv、原生离屏渲染和 FFmpeg 内嵌字幕 fixture）；无跳过项。
- OpenSpec 全量严格校验 **30/30**、`git diff --check`、新增页面/登录英文资源完整性及打包脚本 Python 语法检查通过。
- OpenSpec 已按用户确认同步归档；较早 `prepare-public-release` 的设置 About 增量已交由本次 change 接管，避免以后归档恢复旧布局。
- 构建产物：`build/bin/bbhouse-qt`。未重新生成 DMG、未推送 tag、未改动真实凭据与用户数据库。

## 扫码测试反馈后的验证（2026-09-19）

- 按用户要求暂时隐藏扫码，仅保留 Cookie 导入；新增页面内帮助和独立 Cookie-Editor 导入说明。
- 应用与全部回归目标构建通过，CTest **41/41** 通过，OpenSpec 严格校验 **30/30** 通过，登录/设置中英文资源完整且差异检查通过。
- 该阶段交付后已收到用户整体确认；本次未操作真实 Cookie 或用户数据库，未生成新的 DMG。

## 帮助分发与导航调整验证（2026-09-19）

- 完整 Cookie 导入 Markdown 已编入应用资源，构建生成的资源清单确认路径为 `:/help/Cookie导入帮助.md`；无需另行复制到发行目录。
- 底部导航调整为设置在上、关于在下；页面 key 路由及缓存保持正确对应。
- 应用和回归目标构建通过，CTest **41/41**、OpenSpec 严格校验 **30/30**、差异检查通过。导航调整已获用户确认；本次未重新制作 DMG。

## 用户验收与归档（2026-09-19）

- 用户反馈“perfect, 同步归档”，按当前最终交付范围完成验收与归档。
- 主规格已同步 `login-onboarding`、`about-ui`、`app-navigation-shell`、`settings-ui`、`video-playback-window`：仅 Cookie 导入与内嵌帮助、设置在上关于在下、CC 默认关闭且不记忆。
- 扫码状态机代码保留但入口隐藏，不代表扫码问题已修复；Windows 原生行为仍需对应平台复测，不将本机回归结果当作 Windows 验证。
- 此次仅同步规格与维护记录，不改应用实现、不重新制作 DMG，也不发布远程 tag。
- 归档后复核：应用与回归目标构建通过，CTest **41/41**、OpenSpec 全量严格校验 **31/31**、差异检查通过。
