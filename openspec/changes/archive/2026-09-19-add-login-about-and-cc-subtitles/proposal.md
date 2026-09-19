## Why

首次使用缺少获取及保存登录凭据的入口；关于信息混在设置中且来源不够清楚；已有字幕 API 未在标准播放器提供选择。统一在 1.0.1 完成这些体验。

## What Changes

- 首次缺少有效主 Cookie 时进入初始化页面，提供 Cookie 文本/文件导入及 Cookie-Editor Header String 导出帮助；设置末尾可重新进入。用户扫码测试反馈有问题，扫码入口暂时隐藏；完整 Markdown 帮助嵌入应用资源随包分发。
- 新建关于页，移入应用信息、版本复制、法律声明；每个依赖及参考项目单列来源链接与一句话介绍，仓库更新为 endcloud/bbhouse-tauri-qt；底部导航顺序为“设置”、“关于”。
- 标准播放器右下弹幕左侧增加 CC 字幕菜单，在线字幕含 AI 项、本地字幕均可选；每次起播默认关闭且不持久记忆。
- 应用及打包版本更新为 1.0.1。

## Capabilities

### New Capabilities
- `login-onboarding`: Cookie 导入、导出帮助与初始化入口（扫码入口暂时隐藏）。
- `about-ui`: 独立关于页与逐项开源归属。

### Modified Capabilities
- `settings-ui`: About 移出设置，增加重新登录。
- `app-navigation-shell`: 关于及初始化页面接线。
- `video-playback-window`: CC 字幕菜单、在线和本地字幕选择。

## Impact

涉及凭据读写、登录控制器、Qt/QML 页面、PlayerApi/PlayerController/libmpv、双语翻译、构建与打包版本。API 和媒体保持既有请求级直连/区域代理边界；不修改真实 Cookie 或数据库，不自动化 UI 验收。
