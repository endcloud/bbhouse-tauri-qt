## Decisions

- Qt 使用 applicationDisplayName=BBHouse，applicationName 保持 bbhouse-qt，避免 QStandardPaths 切换到新目录而丢失账号和历史。
- 主窗口标题、关于页名称及复制信息同步更新中英文；功能窗口继续使用登录、播放器或当前内容标题。
- 版本唯一来源为 CMake PROJECT_VERSION；Qt 与 Windows VERSIONINFO、macOS Info.plist/DMG 使用同一版本。
- macOS 外层包名为 BBHouse.app，内部可执行文件与 bundle ID 保持稳定；Linux desktop ID、Exec 与图标名保留兼容。

## Validation

CMake 构建、完整 CTest、OpenSpec strict 校验，检查中英文资源、Windows RC 编译和打包脚本语法及元数据。最终原生显示由用户复测，发布包待下次打包更新。
