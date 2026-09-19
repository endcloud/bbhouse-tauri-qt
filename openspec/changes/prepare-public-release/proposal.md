## Why

仓库即将公开，需要准确的开源归属、隐私边界、新用户预设与可复现的双平台 tag 构建；历史“个人自用”声明已不适用。

## What Changes

- 项目链接统一为 https://github.com/endcloud/bbhouse-tauri-qt；本项目代码声明 GPL-3.0-only，保留第三方原许可证并补参考来源。
- 审查持久化、Git 当前树与可达历史，补忽略规则、关闭可能泄漏凭据的原始 mpv 日志。
- 只修改缺省设置，不迁移或覆盖用户现有偏好：跟随系统、localhost:7890、指定弹幕与播放预设。
- tag push 构建 Windows x64 与 macOS arm64，上传清楚标识的编译产物；不冒充已完成依赖封装的独立发行包。
- 提供 mpv 分发义务与体积实测；用户决定保留 Segoe Fluent Icons，并将其作为正式发布前阻塞项记录。

## Capabilities

### New Capabilities
- `release-readiness`: 发布信息、隐私、自动构建与第三方许可边界。

### Modified Capabilities
- `settings-ui`: 新用户预设。About 独立页面和最新仓库入口由后续 `add-login-about-and-cc-subtitles` 接管，避免归档时恢复旧设置 About 区。
- `video-playback-window`: 新用户预缓存图像、1080P 和 H.265 偏好。

## Impact

涉及偏好读取、播放器初始偏好、设置页双语文案、日志、Git 忽略、构建配置、GitHub Actions 与说明文档。UI/UX 由用户手测。此次不发布远程 tag/Release、不更改用户配置或数据库、不重写 Git 历史。
