# BBHouse 2.0.3（2026-09-21）

用户要求版本更新为 2.0.3 并再次生成 Windows Release。按照本次明确约定，单纯版本更新和打包无需 OpenSpec；约定已写入 AGENTS.md 和个人项目记忆。

本轮基于当前已提交代码（原 HEAD `d2b36e1`，含近期页面内存优化）打包，不回退至 2.0.2。CMake 统一提供运行时及 Windows 主程序/服务的版本信息；启动入口为 BBHouse.exe。现有未跟踪 `.codegraph/` 是本地生成索引，加入忽略且保留，不提交索引数据。

发布继续使用精简自包含脚本，保留包内视频渲染、SMTC 状态、Qt/QML、PE 依赖及媒体工具验证。已有发布目录保留，重名加时间戳，不推送 tag 或远程 Release。

## 验证

- Release 主程序、原生服务和全部 regression-tests 构建成功；两份 EXE 的文件和产品版本均为 2.0.3。
- CTest **38/42 通过**。`player-runtime`、`preferences`、`popular-page`、`history-controller` 四项与上轮相同的既有失败继续保留，未在打包任务扩大修复范围。原生视频与 Windows 系统媒体测试通过。
- 复用此前精简包的静态 FFmpeg/aria2，暂存包与最终发布目录均通过隔离验证：运行时报告 BBHouse 2.0.3，真实视频/SMTC、57 个 PE 文件依赖和 FFmpeg/aria2/curl 检查通过。

## 交付

最终目录：`D:\release\BBHouse[e774d74]`，启动 `BBHouse.exe`。源码提交 `e774d74`，**232 个文件、310219286 字节（295.85 MiB）**；231 条 SHA-256 全量核验通过。后续文档提交仅记录交付，不改变包内代码。

本次两份暂存包、异常退出的隔离历史测试目录及构建/测试/打包日志和报告已清理，正常构建输出、build/deploy、既有发布包和 CodeGraph 索引保留。没有创建或更新 OpenSpec change。

## 内存修复后重新打包（2026-09-21）

- 按用户要求沿用版本 **2.0.3**，源码提交 `a2c090c`，包含[内存优化可靠性修复](内存优化修复与杜比用例验证.md)。本次仅打包，不修改或归档 OpenSpec。
- 发布目录：`D:\release\BBHouse[a2c090c]`，双击 `BBHouse.exe` 启动；**232 个文件、310291913 字节（295.92 MiB）**。
- 使用 `scripts/package-release.ps1 -OutDir D:\release` 重新构建 Release/deploy；FFmpeg 和 aria2 复用此前 `BBHouse[e774d74]` 包内的静态版本，SHA-256 与既有发布记录一致。
- 同一源码此前完整构建及 CTest **46/46通过**；本次暂存包与最终发布目录再次通过隔离 Qt/QML、真实视频像素/进度/重开、Windows 原生媒体控制、57 个 x64 PE 依赖闭包及 FFmpeg/aria2/curl 检查。最终包 **231 条 SHA-256 全量核验通过**。
- 构建输出和验证日志、已有发布目录保留。项目内 `build/release-stage` 删除被自动审批审查以 `blocked by policy` 拒绝，暂存目录仍保留。未推送 tag 或远程 Release。
- 杜比高画质主要内存台阶仍待后续渲染管线优化，本包不代表该问题已解决；页面与在线播放仍待用户手测。

## 按项目记忆重新打包（2026-09-26）

- 沿用 **BBHouse 2.0.3**，不创建、更新或归档 OpenSpec change。保留工作区已有的打包精简改动，修正空目录清理顺序为从深到浅，避免删除父目录后继续遍历已删除的子目录；包内验证说明改为引用当前发布记录。
- Release 主程序、原生服务和 regression-tests 构建成功，CTest **48/48 全部通过**。首次 deploy 链接时 FluentUI DLL 被正在运行的测试占用；测试结束后重试构建成功。
- 源码与打包脚本提交：`9c13d4fdec299f88ceae46f9ce4ae1166b4b9f13`。正式打包前确认工作区干净；发布目录为 `D:\release\BBHouse[9c13d4f]`，启动入口 `BBHouse.exe`。
- 完整精简包 **192 个文件、310212663 字节（295.84 MiB）**；移除未使用的对话框主题和空目录。静态 FFmpeg/aria2 复用既有发布包并核对 SHA-256；Qt、FluentUI、libmpv、Vulkan Loader 及运行库随包，curl 使用 Windows 10/11 系统版本。
- 暂存包与最终发布目录均通过隔离 Qt/QML、真实视频像素/进度/关闭重开、Windows 原生媒体状态回读、57 个 x64 PE 依赖闭包，以及 FFmpeg 合并/音频/封面和 aria2/curl 本机下载验证。最终包 **191 条 SHA-256 全量核验通过**。
- 本轮两个项目内暂存包及临时日志/报告已清理，正常构建输出、原有暂存目录和全部既有发布包保留。后续文档提交仅记录交付，不改变包内源码；未推送 tag 或远程 Release。在线业务和 UI 仍由用户手测，杜比内存限制沿用前述记录。

## 在线 HTTPS 回归修复包（2026-09-26）

用户随后报告 `BBHouse[9c13d4f]` 在线媒体加载失败。已定位为禁用 mpv curl 后 FFmpeg/OpenSSL 缺少 Windows 系统 CA，补齐信任配置并保持 TLS 验证与媒体直连，详见[HTTPS 根因与验证](Windows在线视频HTTPS信任修复.md)。

修复包：`D:\release\BBHouse[4a3df82]`，仍为 **2.0.3**，**192 个文件、310246196 字节（295.87 MiB）**。完整 CTest **48/48 通过**，公共 HTTPS 和匿名 B 站 DASH 双流加载成功；暂存及正式包新增 HTTPS 证书拒绝/可信加载/独立音轨门禁通过，真实视频/SMTC/依赖/工具门禁保留，最终 **191 条 SHA-256 全量核验通过**。源码提交 `4a3df82`，已有发布包保留，待用户在线手测。
