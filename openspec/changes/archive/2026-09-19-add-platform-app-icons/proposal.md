## Why

应用尚未统一设置原生可执行文件、运行时窗口及发行包图标。用户已提供全平台图标素材，需接入构建与分发。

## What Changes

- 将用户提供的 PNG、macOS PNG、ICO 与 ICNS 复制到项目资源目录，构建不再依赖外部素材路径。
- Windows 主程序及原生服务宿主嵌入 ICO；Qt 与 FluentUI 使用平台对应的 PNG，关于页使用应用图标。
- macOS 打包附带 ICNS，并在 Info.plist 声明、验证；按 Dock 反馈为 macOS PNG/ICNS 添加约 20% 的总透明边距，使图标主体接近系统应用大小。
- Linux 提供 hicolor 图标、desktop 入口及 CMake 桌面集成安装组件，Qt desktop ID 与入口一致。

## Capabilities

### Modified Capabilities
- `qt-app-scaffold`: 应用全平台图标资源、运行时与打包接线。

## Impact

涉及应用资源、CMake、Qt/QML 初始化和 macOS 打包脚本；保留无头入口及当前标题栏布局，不修改外部素材、不操作真实账号数据。平台原生图标观感由用户手测。
