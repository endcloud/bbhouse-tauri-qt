## Context

现有主程序是裸可执行文件，macOS 由脚本组装 .app，Windows 通过 deploy 复制 exe；尚无 Linux 完整发行包。FluentUI 的窗口图标与 Qt 应用图标分别设置。

## Decisions

- 首次原样导入四份用户素材，PNG 嵌入 Qt 资源；根据 Dock 尺寸反馈，将 macOS 主体居中缩至 824/1024，四边各留 100 像素透明边距，并从同一母版生成全尺寸 ICNS。原始 macOS PNG 保存在 source 子目录，生成脚本避免重复缩小；其他平台使用通用 PNG。
- Windows RC 配置绝对 ICO 输入路径，避免编译工作目录影响；资源编入主程序和原生 helper。
- macOS ICNS 由打包脚本复制后签名，并校验 Info.plist 指向的文件与素材一致。
- Linux 安装 DesktopIntegration 组件到 applications 和 hicolor/1024x1024/apps，使用固定 desktop ID。该组件仅交付桌面资源，应用及运行库仍由发行环境提供。
- 保持当前主窗口隐藏标题图标的布局，仅替换已有关于页占位图标并设置 FluentUI 默认窗口图标。

## Validation

构建应用及全部回归目标、CTest、资源格式和 Windows RC 编译检查、macOS 图标打包逻辑验证及 OpenSpec strict 校验。Windows/macOS/Linux Shell 图标缓存、Dock/任务栏观感待用户手测，不做 UI 自动化。
