## Why
本 change 原含 Windows 任务 XML 句柄修复；用户后续反馈编码错误并取消任务计划方案，该部分由 `replace-windows-scheduler-with-service` 替代，本 change 剩余范围仅为媒体 CDN。播放端现有排序仅识别 mcdn，durl 不保留完整候选，DASH 音频只消费首地址，直播逐轨排序会使某轨 P2P 先于其他普通 CDN。

## What Changes
- 统一媒体地址分类：已知公有云 CDN 优先、其他普通 HTTP(S) 次之、已知 mcdn/PCDN 最后；保持原始签名地址、同级顺序和去重。
- UGC/PGC DASH 音视频与 durl、直播同档位所有候选应用一致策略；保留备选并支持音频异步失败/超时回退。
- 补充离线与本地媒体回归、播放手测说明。

## Impact
涉及 video-playback-window、live-ui。接口权限和区域 API 请求级代理边界保持；不注册真实任务、不修改真实数据库、不输出真实媒体签名或 Cookie。Windows 原生/UI 验收由用户手测。
