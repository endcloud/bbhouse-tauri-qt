## ADDED Requirements

### Requirement: 区域 API 代理隔离

应用 MUST 仅对港澳台模块标记的剧集详情与 PGC 播放地址 API 使用代理；配置 MUST 是每任务独立快照，MUST NOT 修改系统、全局应用代理或其他任务的路由。普通 API MUST 显式直连。PGC 初始 DASH、durl 回退、切集和清晰度切换 MUST 保留正确区域标记；UGC MUST NOT 因标记误用代理。

#### Scenario: 同时浏览与播放区域番剧
- **WHEN** 港澳台番剧请求通过配置代理执行且普通历史 API 同时请求
- **THEN** 仅区域请求抵达代理，普通 API 直连且相互不改变配置

#### Scenario: 修改代理配置
- **WHEN** 修改设置后再次请求区域剧集详情
- **THEN** 使用新配置，旧缓存及旧在途详情不能覆盖新请求

### Requirement: 媒体始终直连

libmpv 的音频与视频连接 MUST 直连，不使用本模块代理、进程代理环境变量或外部 mpv 配置。MUST 保留服务端登录/地区/会员/试看限制的错误语义，MUST NOT 在直连失败时悄悄给媒体套代理。

#### Scenario: 环境存在代理
- **WHEN** 进程环境配置 http_proxy 且区域播放已解析到媒体地址
- **THEN** 媒体访问直连服务器，本地代理只接收区域 API 流量
