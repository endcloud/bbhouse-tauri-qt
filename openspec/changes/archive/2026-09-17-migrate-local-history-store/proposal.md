# Proposal: migrate-local-history-store

## Why

本地 SQLite 存储是同步与浏览能力的地基(契约见 `local-history-store` 能力规格)。把原 C# `HistoryStore` 迁移为 Qt/C++(Qt Sql / QSQLITE):建表迁移、事务化 upsert、分页查询、JSON 导出、同步审计、播放位置表。

## What Changes

- 新增 `app/core/HistoryStore.h/.cpp`(阻塞式,约定在工作线程使用):
  - 建表 videos / view_records / sync_runs / cursor_snapshots / playback_positions;旧版 history_items 一次性非破坏迁移;source 列幂等迁移;WAL + busy timeout;
  - 页级事务 upsert(cursor 快照 + video upsert + view_record INSERT OR IGNORE + view_count 重算),UNIQUE(video_key, view_at) 幂等;
  - 分页查询(30 条/页,全字段离线)、JSON 导出、StartSyncRun/FinishSyncRun 审计、最近运行查询;
  - playback_positions 写穿 UPSERT + 查询(播放器变更复用)。
- 无规格 delta:仅实现既有规格,video_key= business:oid:kid 约定与幂等语义不变。
- 数据库文件路径经 `AppPaths::dbPath()`(QStandardPaths 数据目录,与原 LocalFolder 语义对齐)。

## Impact

- Affected specs: 无修改(实现 `local-history-store`)
- Affected code: 新增 `app/core/HistoryStore.*`;app/CMakeLists 注册
