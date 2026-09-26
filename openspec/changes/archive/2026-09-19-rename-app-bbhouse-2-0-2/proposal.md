## Why

用户要求将应用全局显示名称统一为 BBHouse，并升级至 2.0.2。

## What Changes

- Qt 应用显示名、主窗口和关于页、中英文名称统一为 BBHouse。
- CMake 版本更新至 2.0.2，关于页与原生分发元数据共享该版本。
- macOS 应用包、DMG 显示名与 Windows 发布目录、Linux 桌面名称使用 BBHouse；Windows 原生资源加入产品名及版本。
- 保留内部程序标识、可执行文件名和数据目录，以兼容已有 Cookie、数据库、偏好与定时服务。

## Capabilities

### Modified Capabilities
- `qt-app-scaffold`: 名称、版本与原生元数据一致性。
- `about-ui`: BBHouse 名称及 2.0.2 版本展示。

## Impact

涉及应用初始化、QML、英文翻译、CMake/RC、平台打包和桌面文件。此轮不重打发行包、不推送远程标签，也不修改发布副本；现有打包方式在下次运行时产出新名称和版本。
