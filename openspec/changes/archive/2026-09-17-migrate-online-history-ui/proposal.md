# Proposal: migrate-online-history-ui

云端观看历史只读瀑布流页(契约: online-history-ui 规格)。骨架为 OnlineHistoryPage:游标翻页池+投影(HistoryApi::fetchPage,video_key 去重,终止信号=空页/游标缺 business/游标同上页);触底阈值续载+布局后补轮(空投影续链:上限5轮+轮间400ms);刷新仅图标按钮;卡片复用 HistoryCard,封面预览/回顶/右键起播对称沿用;不落库无分页栏;标题栏搜索投影接入;缓存页语义同外壳。
