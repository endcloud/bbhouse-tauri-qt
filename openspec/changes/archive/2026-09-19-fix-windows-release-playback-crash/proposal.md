## Why

Windows Release 包在线视频和本地视频在首帧后卡死退出，事件日志记录访问冲突。现有发布验证仅覆盖音频，不能验证视频渲染及起播后的原生组件。

## What Changes

- 核查发布流程、libmpv ABI/实际加载文件与首帧后的原生调用。
- 使用隔离数据和合成视频复现并修复根因，补齐有实际像素的回归及发布验证。
- 重新构建并交付新提交号目录，保留旧包。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

无；恢复现有播放行为并补全验证，使用 `skip_specs: true`。

## Impact

Windows 播放原生组件、自动回归和打包脚本；不读取真实凭据/历史、不注册服务、不修改外部组件。
