# Proposal: migrate-dynamics-ui

关注动态流页(契约: dynamics-ui 规格)。DynamicApi feed/all offset 翻页;客户端六类七档筛选(全部/视频/番剧影视/直播/专栏/动态/其他;条目级 ARTICLE 精判,转发取 orig,OPUS/DRAW→动态,LIVE→直播);池+投影(切档零网络回顶);续载至当前筛选新增≥24卡或6页上限;aid 去重+出现N次角标;视频档分区行(VideoZones 静态表+VideoZoneApi 补查,250ms 串行+会话缓存+空匹配门控);卡片映射复用 HistoryCard(business 标签 dyn-*/图文/直播);右键视频起播;筛选档位持久化(App.Dynamics.Filter)。
