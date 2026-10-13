## MODIFIED Requirements

### Requirement: 媒体始终直连

libmpv 的音频与视频连接 MUST 直连，不使用本模块代理、进程代理环境变量或外部 mpv 配置。MUST 保留服务端登录/地区/会员/试看限制的错误语义，MUST NOT 在直连失败时悄悄给媒体套代理。Windows HTTPS 媒体 MUST 保持 TLS 证书验证，且使用系统可信 CA；若传输后端不能直接读取系统信任，应用 MUST 显式提供系统 CA，不得以关闭证书验证恢复播放。CA 临时文件 MUST 保持到共享媒体内核完全销毁后清理。

#### Scenario: 环境存在代理
- **WHEN** 进程环境配置 http_proxy 且区域播放已解析到媒体地址
- **THEN** 媒体访问直连服务器，本地代理只接收区域 API 流量

#### Scenario: Windows HTTPS 直连加载
- **WHEN** Windows 内核通过 FFmpeg 加载系统 CA 信任的 HTTPS 视频或独立音轨
- **THEN** HTTPS 加载成功，TLS 验证开启且媒体不使用进程代理

#### Scenario: 不可信 HTTPS 证书
- **WHEN** 媒体服务器提供不可信证书
- **THEN** 内核拒绝连接并返回可见加载错误，不关闭 TLS 验证或套用代理

#### Scenario: 渲染器保留内核
- **WHEN** Qt 客户端已析构而渲染器仍持有共享 mpv handle
- **THEN** CA 临时文件仍存在，最后一个 handle 持有者释放并完成内核销毁后文件自动清理
