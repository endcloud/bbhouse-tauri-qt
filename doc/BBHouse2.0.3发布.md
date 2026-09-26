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
