# Tasks: migrate-local-history-store

## 1. 存储层

- [x] 1.1 `HistoryStore.h/cpp`:WAL + QSQLITE_BUSY_TIMEOUT 30s、建表与幂等迁移(videos/view_records/sync_runs/cursor_snapshots/playback_positions;history_items 旧表一次性迁入后保留不动;source 列幂等 ALTER)
- [x] 2.1 页级事务 upsert:cursor 快照 + video upsert(仅更新的 view_at 前移展示字段)+ INSERT OR IGNORE view_record + view_count 重算
- [x] 2.2 查询:分页(含 view_records 装载)、JSON 导出、审计(Start/Finish + 最近 N 条)、播放位置 UPSERT/查询(超时参数化)
- [x] 3.1 构建通过;`tools/store_test` 无头自测 18/18 ALL PASS(幂等 upsert/计数/排序/导出/位置/二次 initialize);归档 + commit
