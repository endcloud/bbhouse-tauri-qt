# Proposal: migrate-bangumi-ui

追番追剧页(契约: bangumi-ui 规格)。四档 SelectorBar(番剧/国创/影视/纪录片)=客户端按 season_type 过滤两桶(1=番剧+国创,2=影视类);两桶标准服务端分页 30/页+桶级页码记忆;竖版 SeasonCard(评分角标/进度文本/简介)+网格布局;搜索=当前页标题投影;选集起播=BangumiApi::fetchSeason 全分集提交剧集模式(PlayerController PlaySeason,集键 pgc:{epid}:0,Progress=last_time 续播);末集停止/选集菜单对勾。
