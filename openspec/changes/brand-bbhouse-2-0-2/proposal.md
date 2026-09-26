## Why

用户已确认 Windows Release 起播修复手测通过，要求应用全局名称改为 BBHouse、版本更新为 2.0.2，并重新交付精简 Release 包。

## What Changes

- 主窗口、关于页及中英文应用品牌统一 BBHouse，Windows 启动文件使用 BBHouse.exe。
- CMake 版本更新为 2.0.2，同步运行时版本、Windows PE 属性及 macOS 包信息；保持已有数据标识兼容。
- 更新两平台打包及 CI 文件引用，发布脚本验证名称和版本后输出 BBHouse[提交号]。
- 记录此前修复验收；清理受自动审批限制的项目临时目录时如实记录结果。

## Capabilities

### New Capabilities

- `app-branding`: 应用名称、平台元数据及版本一致性。

### Modified Capabilities

- `about-ui`: 关于页品牌和版本展示使用当前应用名称及构建版本。

## Impact

应用入口、QML/翻译、Windows 资源、打包脚本、文档和 OpenSpec。不迁移用户配置/数据库，不修改已安装服务。
