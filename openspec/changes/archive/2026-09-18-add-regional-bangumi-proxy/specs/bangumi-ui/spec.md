## ADDED Requirements

### Requirement: 港澳台独立标签

番剧页 MUST 增加“港澳台”标签，从全部已追番页筛选标题含“僅限港澳台地區”的条目，兼容简体和括号变体；MUST 按 seasonId 去重后每页 30 条本地分页。常规分类 MUST 排除这些条目。追番发现 API MUST 直连，地区详情与播放 API MUST 通过 regional-bangumi-proxy 能力路由。搜索 MUST 先过滤完整区域结果再分页，刷新失败 MUST 保留旧结果并允许重试。

#### Scenario: 区域番剧位于后续追番页
- **WHEN** 用户首次进入港澳台标签且该作品不在追番第一页
- **THEN** 扫描后续页后仍呈现该作品，区域页总数仅包含符合条件的去重结果

#### Scenario: 地区详情与起播
- **WHEN** 点击港澳台卡片或详情中的分集
- **THEN** 详情和播放解析携区域标记，普通番剧起播无该标记
