## Why

维护者要求将 Qt/QML 版本发布到原 Tauri 仓库 endcloud/bbhouse-tauri-qt，以 tauri 分支保留旧 master 的完整历史，并在 ~/Documents/code_g/bbhouse-tauri-qt 创建无原 Git 历史、无用户数据的独立发布仓库。

## What Changes

- 在更新默认 master 前，将旧 master 的确切提交保存为远端 tauri 分支。
- 按经过审计的跟踪文件清单复制源码、全部文档、OpenSpec 工作记录和第三方许可，排除原 .git、外部 b3 符号链接及忽略的用户数据/构建产物。
- 初始化新的 master 根提交，配置目标远端，并使用绑定旧提交的 force-with-lease 发布。
- 缩短登录测试的人工 Cookie 值，保留百分号编码和单次解码断言，使严格隐私扫描不再误报该 fixture。

## Capabilities

### New Capabilities
- `repository-publication`: 原 Tauri 分支归档、独立 Qt 源码历史与发布完整性。

## Impact

应用功能不变。操作涉及当前项目的记录及合成测试、新本地发布目录、远端 tauri/master 分支。既有 main/dev、tag、Release 保留。现有字体、头像许可与原生手测待办继续如实保留，不因源码迁移标记完成。
