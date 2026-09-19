## ADDED Requirements

### Requirement: 二级代理设置

设置 MUST 提供可折叠“代理设置”区域，包含 HTTP/SOCKS5/关闭、host、1–65535 端口与可选用户名/密码认证；默认值 MUST 为 HTTP、localhost、7897。MUST 明示仅用于港澳台 API，媒体直连。保存 MUST 先整体校验，失败保留原配置。非秘密配置 MUST 持久化，密码 MUST 仅在本次运行内保存并明确标注。

#### Scenario: 配置本地 Clash
- **WHEN** 用户保存 HTTP、localhost、7897 且认证为空
- **THEN** 区域 API 使用无认证的本地 HTTP 代理，其他 API 与媒体保持直连

#### Scenario: 无效配置
- **WHEN** host 包含 URL 路径或端口越界
- **THEN** 设置提示错误且不部分覆盖已有配置
