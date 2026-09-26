## Why

用户要求在 `D:\release` 生成按项目名和提交号命名、包含全部运行依赖的精简 Windows Release 包。现有脚本没有附 aria2/FFmpeg，遗漏 libmpv 的 Vulkan Loader，并使用可能访问开发机配置的普通启动入口验证。

## What Changes

- 更新 Windows 发布脚本：裁剪开发文件、未使用主题/插件和重复资源，只对暂存副本去除符号。
- 补齐下载工具与 Vulkan Loader，检查完整 PE 导入依赖并记录版本、来源与文件哈希。
- 扩展隔离部署验证用于 Windows，验证包内 Qt、libmpv、本地音频、SQLite、图片、QML 和外部工具。
- 输出新目录 `bbhouse-qt[提交号]`；重名时附时间戳，保留已存在发布目录。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

无，打包和验证工具变更，不改变播放、下载或服务业务契约；使用 `skip_specs: true`。

## Impact

Windows 发布脚本、部署验证入口、包体依赖检查及交付文档。不读取或分发真实 Cookie/用户数据库，不安装真实服务；只创建本次发布目录，不删除项目外已有文件。
