## Why

本次用户明确要求完善“本地历史”：将云端观看历史持久保存到 SQLite、保留重复观看事件、展示已保存标记，并从该页管理系统定时服务。原 change 的“待播队列/关窗归档 JSON”不属于这次请求；保留 change 名称但以本次范围替换其执行计划，不宣称旧待播能力已实现。

## What Changes

- 复用并加固原生 C++/Qt HistoryApi、HistorySyncRunner、HistoryStore，按真实观看时间追加事件、相同事件幂等，不覆盖已保存事件。
- 本地历史主体沿用在线历史的 300px 卡片、16px 间距、最短列瀑布流和整体居中；保留本地分页与页内搜索。
- 本地卡片新增常驻“已记录”及次数 badge，悬停逐次显示真实观看时间和进度；单次也显示。共享卡片其他页面默认行为不变。
- 增加服务管理按钮；未注册先提示确认注册，已注册打开独立管理窗口；异步查询、读取和数据库权限错误可见且可恢复。

## Impact

涉及 history-browser-ui、local-history-store、HistoryController、LocalHistoryPage、HistoryCard；服务窗口与后端由 migrate-scheduled-crawler-service 交付。旧 local-records-ui 待播/关窗归档目标继续作为独立遗留范围，不能因本 change 完成而视为实现。
