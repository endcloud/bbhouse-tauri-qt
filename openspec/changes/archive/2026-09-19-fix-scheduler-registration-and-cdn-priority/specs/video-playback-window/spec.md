## ADDED Requirements

### Requirement: 点播媒体 CDN 优先与完整备选
UGC 和 PGC 的 DASH 视频、音频以及 durl 媒体候选 SHALL 优先使用响应内可识别的公有云 CDN，其次为其他普通 HTTP(S) 节点，已知 mcdn/PCDN 节点仅作为最后备选。同类 SHALL 保持服务端次序并去重。地址 MUST 保持原始签名内容，MUST NOT 通过改写域名或额外 API 重试绕过服务端权益。选择 SHALL 在既有档位、编码和音频规格内进行。

#### Scenario: 主地址为 PCDN 而备选为云 CDN
- **WHEN** 视频或音频主地址为 mcdn/pcdn/szbdyd 节点，备选含可识别云 CDN
- **THEN** 先加载云 CDN，失败后依序尝试剩余候选，P2P 地址仍保留在末尾。

#### Scenario: 只有 P2P 候选
- **WHEN** 响应只有有效 P2P 地址
- **THEN** 仍可按原顺序尝试播放，不将候选过滤为空。

#### Scenario: 音频失败或超时
- **WHEN** DASH 主视频已装载而当前音轨加载失败或超时
- **THEN** 异步尝试下一音频候选，耗尽后进入既有失败回退；切播和关闭后旧回调不得影响新媒体。

#### Scenario: 单流回退的备用地址
- **WHEN** 播放回落 durl 且首候选装载失败或超时
- **THEN** 继续尝试同一分段的其余排序候选，耗尽才报告最终失败。
