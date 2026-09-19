# Proposal: migrate-watchlater-ui

稍后再看页(契约: watchlater-ui 规格)。ToviewApi 单次全量(条目按 add_at 降序,解析即 HistoryItem,与库内 archive 键互通);无游标/续载;失效稿件(state<0)占位处理;PGC 形态右键起播(TryParsePgcLocation);刷新保旧卡;搜索=全量池投影零网络;缓存页语义。
