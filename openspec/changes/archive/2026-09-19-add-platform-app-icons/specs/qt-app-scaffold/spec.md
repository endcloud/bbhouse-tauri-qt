## ADDED Requirements

### Requirement: 全平台应用图标
应用 SHALL 使用项目内用户提供的图标，PNG 资源 SHALL 内嵌到程序；Qt 应用图标、FluentUI 默认窗口图标及关于页 SHALL 使用同一品牌图形，macOS 选择透明圆角 PNG；PNG 和 ICNS 各尺寸 SHALL 保持约 80% 的居中主体与透明外围留白，使 Dock 中的视觉大小接近系统应用。无头任务入口 MUST NOT 为图标初始化 GUI。

#### Scenario: 运行时图标
- **WHEN** 用户从任意工作目录启动 GUI 并进入关于页
- **THEN** 应用及关于页读取内嵌图标，无需原始素材目录；现有主窗口隐藏标题图标的布局保留。

### Requirement: 平台图标分发
Windows 主程序及原生服务宿主 SHALL 嵌入多尺寸 ICO；macOS .app SHALL 包含 ICNS，Info.plist 的 CFBundleIconFile SHALL 指向它，打包验证 SHALL 检查文件完整及来源一致。Linux SHALL 提供 desktop 入口与 hicolor PNG 的 DesktopIntegration 安装组件，Qt desktop ID SHALL 匹配入口名。

#### Scenario: Windows 可执行文件
- **WHEN** 使用 MinGW 或 MSVC 构建并分发可执行文件
- **THEN** 图标已作为原生资源包含在 exe 中，不依赖外置 ICO。

#### Scenario: macOS 应用包
- **WHEN** 运行 macOS 打包脚本
- **THEN** 图标在签名前写入 Resources，并在原包及迁移检查中验证；缺失或不一致时阻止交付。

#### Scenario: Linux 桌面集成
- **WHEN** 安装 DesktopIntegration 组件且 bbhouse-qt 已在 PATH 中可运行
- **THEN** desktop 入口以 bbhouse-qt 启动，Icon 名称解析到安装的 hicolor 图标，并与 Qt desktop ID 对应。

#### Scenario: macOS Dock 视觉大小
- **WHEN** 用户在 Dock 中将本应用与系统应用并排显示
- **THEN** 图标主体不铺满画布；1024 像素母版主体为 824 像素、四边各 100 像素透明留白，ICNS 各尺寸保持相同比例。
