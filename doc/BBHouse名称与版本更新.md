# BBHouse 2.0.2 名称与版本更新

日期：2026-09-19。OpenSpec：`rename-app-bbhouse-2-0-2`。

用户已确认图标及 macOS Dock 尺寸修复通过，`add-platform-app-icons` 已同步主规格并归档。名称与版本调整已于 2026-09-19 获用户确认，主规格已同步并[归档](../openspec/changes/archive/2026-09-19-rename-app-bbhouse-2-0-2/)。

## 更新内容

- 主窗口、关于页名称及复制版本信息、中英文品牌名统一为 **BBHouse**。
- Qt 应用显示名称设为 BBHouse，CMake 项目版本升至 **2.0.2**，关于页自动读取该版本。
- Windows 主程序和服务宿主的原生 VERSIONINFO 显示 BBHouse 产品名，文件与产品版本来自 CMake；ICO 保留。
- macOS 下次打包生成 `BBHouse.app`，CFBundleName/CFBundleDisplayName 和 DMG 卷名同步 BBHouse，版本来自 CMake。
- Windows 默认发布目录为 `BBHouse[提交号]`（同名追加时间戳），脚本从 CMake 读取版本并校验包内名称/版本，见 [Windows 发布记录](BBHouse2.0.2发布.md)。
- Linux 桌面入口各语言名称统一为 BBHouse。

内部 applicationName、组织名、可执行文件名 `bbhouse-qt`、bundle ID、Linux desktop ID 及服务标识保持兼容。已有 Cookie、SQLite、设置与服务运行路径继续使用原位置，不搬迁或改写用户数据。

本轮修改当前 Qt 工作区，未同步外部发布副本、未推送 tag、未重新制作发行包。旧 `.app/DMG` 保持原版本，下一次打包应用新名称和版本。

## 后续发行复测

启动新构建，检查主窗口名称与关于页为 BBHouse、版本为 2.0.2，复制信息一致；切换英文后结果相同。确认已有账号与历史仍可读取。下次打包时检查 Finder/Dock、Windows 文件属性与 Linux 应用菜单中的品牌名。

## 验证结果

- macOS Qt 6.11.2 应用与全部回归目标构建通过，CTest **41/41** 通过。
- Windows ICO/VERSIONINFO 经 MinGW windres 编译成功，产物包含 BBHouse 与 2.0.2；中英文品牌资源一致。
- macOS Python 脚本语法、Info.plist/DMG 命名及新旧发布目录清理筛选检查通过；Windows PowerShell 语法与发布目录命名检查通过。
- OpenSpec 全量严格校验 **31/31**、差异检查通过，临时验证产物已清理。

## 用户验收与归档（2026-09-19）

用户反馈“ok, 同步归档”。已同步 `about-ui` 和 `qt-app-scaffold` 主规格，确认 BBHouse 显示名、2.0.2 版本及已有数据路径兼容要求。此轮仅同步文档与规格；发行包尚未重建，原生发行外观在后续打包时复核。

归档后复核：构建通过，CTest **41/41**、OpenSpec 全量严格校验 **30/30**、差异检查通过。

## 2.0.2 本机发行包重建（2026-09-19）

后续按用户要求重建 macOS Release，交付 `BBHouse.app` 与 `BBHouse-2.0.2-macos-arm64.dmg`；具体产物、校验值和验证结果见 [macOS 打包记录](macOS应用与DMG打包.md)。内部标识及用户数据路径继续保持兼容。本轮未创建远程 tag 或 GitHub Release。
