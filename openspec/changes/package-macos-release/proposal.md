## Why

用户要求在本机交付独立 macOS Release .app 和可拖拽安装的 DMG。现有裸可执行文件依赖开发机 Qt/Homebrew，不能作为独立应用。

## What Changes

- 增加可复用 macOS 打包脚本，将 Release 主程序、FluentUI、Qt/QML 插件和 libmpv 动态依赖部署为 arm64 .app。
- 修正 Mach-O 加载路径，检查依赖闭包、架构与最低系统要求，逐项签名及验证。
- 增加不读真实凭据/配置的部署冒烟入口，检查包内 QML、SQLite、图片插件和真实 libmpv 本地媒体播放。
- 制作含 Applications 快捷入口与图标布局的只读 DMG，挂载后验证内容。
- macOS .app 的 Cookie 从已有用户数据目录读取，不修改签名 bundle；不迁移或复制本机凭据。
- 附依赖版本/许可证与已知公开发行许可阻塞说明。此次是本地打包，不上传远程发行平台。

## Capabilities

### New Capabilities
- `macos-release-package`: 独立应用、拖拽 DMG 与可复现验证。

## Impact

主程序增加显式部署诊断参数；默认运行行为及用户数据保持不变。内嵌字体按用户先前决定保持，许可清理不在本次范围。本机无 Developer ID，默认 ad-hoc 签名、不公证；系统版本下限以实际捆绑依赖为准。

## 2026-09-19 下载工具补充

按用户重新打包要求，将 aria2c、FFmpeg、Homebrew curl 和非系统动态依赖一起部署，补齐实际组件许可材料和离线工具冒烟。新包验证成功后清理项目 build/release 内旧版交付目录，保留构建缓存与用户数据。
