## Why

已追番中的港澳台限定内容需要通过本地 Clash 等代理读取地区对应 API。当前番剧没有独立地区标签和请求级代理选择；全局代理会让媒体流与其他功能也走代理。

## What Changes

- 增加“港澳台”标签，从已追番全部页识别地区限定标题，并独立分页。
- 港澳台剧集详情及 PGC 播放地址解析使用独立代理快照，其他 API 与音视频保持直连。
- 设置增加可折叠代理区域：类型、host、端口、可选用户名/密码认证；用户已明确“加密”为认证。
- 使用 md28233436 验证媒体资料、地区 API 路由与直连媒体边界；保留真实服务端错误。

## Capabilities

### New Capabilities
- `regional-bangumi-proxy`: 区域 API 独立代理与媒体直连。

### Modified Capabilities
- `bangumi-ui`: 地区标签、全追番发现和分页及播放标记。
- `settings-ui`: 请求范围明确的二级代理设置。

## Impact

BilibiliApiClient、BangumiApi/Controller/Page、PlayerController/MpvClient、AppPreferences/SettingsPage、离线网络代理与区域列表回归。不会更改系统或 Clash 的代理/节点配置；不写云端追番或观看记录用于测试。
