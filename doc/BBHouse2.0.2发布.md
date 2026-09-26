# BBHouse 2.0.2（2026-09-19）

OpenSpec：`brand-bbhouse-2-0-2`。用户确认此前 Windows Release 起播修复“测试完成，pass”，该修复已归档至 `openspec/changes/archive/2026-09-19-fix-windows-release-playback-crash/`。

## 名称与版本

- 主窗口、关于页和中英文复制版本信息统一为 BBHouse；Qt 应用显示名为 BBHouse。
- CMake 版本为 2.0.2，运行时关于页、Windows 产品版本/文件版本和 macOS bundle 版本都取同一来源。
- Windows 用户启动文件改为 `BBHouse.exe`，文件描述和产品名称为 BBHouse；原生服务宿主也包含 2.0.2 版本信息。
- macOS 输出 `BBHouse.app`、`BBHouse-2.0.2-macos-arm64.dmg`，显示名/卷标统一；Linux 中英文桌面名称均为 BBHouse。这两平台本轮仅静态校验，未在 Windows 上声称完成原生打包。
- Qt 内部 applicationName、设置命名空间、bundle identifier 和服务宿主内部文件名保持兼容，因此不因品牌更新迁移用户设置和历史库。已有 Windows 快捷方式仍指向旧包，新包从 BBHouse.exe 启动。

## 发布与验证

Windows 脚本默认输出 `D:\release\BBHouse[提交号]`，同名正式目录和暂存目录追加时间戳。发布前验证主程序及服务的文件属性、程序运行时名称/版本，还需通过原生视频像素、进度、关闭重开、SMTC 状态回读、Qt/QML、PE 依赖闭包、FFmpeg 和回环下载检查。

Release 主程序和全部回归程序构建通过；CTest **37/41 通过**。本地 H.264 OpenGL 渲染、Windows SMTC、关于页/登录页、下载、服务入口等通过；`player-runtime`、`preferences`、`popular-page`、`history-controller` 与上轮相同的四项既有失败保留，详情见 [起播修复记录](WindowsRelease起播闪退修复.md)。不因用户对播放器的 pass 自动视为修复。

Windows 主程序/服务原生文件属性已核验 ProductName=BBHouse、ProductVersion=FileVersion=2.0.2，主程序 OriginalFilename=BBHouse.exe。精简暂存包和最终发布目录均报告 `DEPLOYMENT_IDENTITY: BBHouse 2.0.2`，真实视频/SMTC、57 个 PE 文件及 FFmpeg/aria2/curl 验证通过。PowerShell 和 Python 语法、OpenSpec 严格验证通过。

最终交付：`D:\release\BBHouse[09df8f0]`，源码提交 `09df8f0`，**232 个文件、310167574 字节（295.80 MiB）**；231 条 SHA-256 全量核验通过。启动 `BBHouse.exe`，旧版发布目录保留。后续文档提交仅记录交付和清理结果，不改变包内代码。

## 清理状态

用户已明确允许清理临时文件/目录。最初强制递归删除被自动审批拒绝；取消强制删除、使用同一 PowerShell 通道的普通删除后审批通过，旧 `build/release-stage`、`build/smtc-regression`、`build/app/history-controller-rzJAnX` 及旧测试日志已清理。本次三份暂存目录、异常退出的隔离历史测试目录、临时构建/打包日志和报告、改名前的过期 build/bin/bbhouse-qt.exe 也已清理；保留正常编译输出与 build/deploy。正式发布包不含测试数据、用户 Cookie 或历史库。
