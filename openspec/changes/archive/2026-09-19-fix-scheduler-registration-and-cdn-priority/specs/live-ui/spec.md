## ADDED Requirements

### Requirement: 直播跨轨 CDN 优先
直播 SHALL 在选定实际清晰度内，对所有可用格式和编码的候选统一应用公有云 CDN、其他普通节点、已知 mcdn/PCDN 的优先级；同类继续原有协议和编码偏好。P2P MUST 仅作为普通 CDN 耗尽后的备选，不因位于首选 FLV 轨而提前加载。

#### Scenario: FLV 仅有 P2P 而 HLS 有普通 CDN
- **WHEN** 相同实际清晰度的 FLV 候选为 P2P、HLS 候选为普通 CDN
- **THEN** 先尝试普通 CDN 的 HLS，再尝试 P2P；不同清晰度地址不得混入。
