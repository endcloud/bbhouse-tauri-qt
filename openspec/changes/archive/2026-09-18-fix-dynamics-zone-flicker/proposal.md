## Why

动态页选择视频分类后，逐条 view 分区补查会替换整个 QVariantList，导致 QML Repeater 销毁并重建所有卡片，持续闪烁。分区按钮也随每次 poolChanged 重建。

## What Changes

- 动态卡片改用稳定的 Qt 列表模型，以视频 aid / 动态 id 为身份执行增量字段更新、插入、删除与移动。
- 分区选项单独通知，保留现有按钮，仅增删实际变化的分区。
- 刷新结果一次发布，避免在新数据到达后先向界面发出空列表。
- 保持串行补查、会话缓存、分类/分区/搜索、去重与续载语义。

## Capabilities

### Modified Capabilities

- `dynamics-ui`: 分区补查时稳定复用卡片与筛选按钮。

## Impact

DynamicsController、新增卡片模型、DynamicsPage 与离线回归；不修改 API 端点或用户数据。
