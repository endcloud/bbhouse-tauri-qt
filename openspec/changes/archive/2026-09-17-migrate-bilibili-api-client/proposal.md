# Proposal: migrate-bilibili-api-client

## Why

API 统一层是所有数据能力(同步/浏览/播放/番剧/关注等)的地基。按 `bilibili-api-client` 能力规格整体迁移为 Qt/C++ 服务层:统一请求管线 + 按域端点模块。原 C# 实现位于 `C:\Users\shizi\source\repos\Bilibili.History\Bilibili.History.Core`,解析怪癖(实测口径)逐一保留。

## What Changes

- 新增 `app/core/`(UI 无关 Qt/C++ 服务层,阻塞式 API,约定在专用工作线程调用):
  - 管线:`ApiErrors`(登录失效/信封code/HTTP状态三态)、`BilibiliApiClient`(QNetworkAccessManager 单例 + UA/Referer/Origin/Cookie 头 + 信封校验 + cookie 三预检)、`JsonHelpers`(容错读取:缺字段=空/0/false/NaN)、`AppPaths`(cookie 定位/数据目录)、`Loc`(核心层 zh 中性文案 + en 翻译)
  - 签名:`WbiSigner`(nav 取键、MIXIN_KEY_ENC_TAB 置换、4h 进程缓存)
  - 模型:`HistoryItem/ViewRecord/HistoryCursor/BilibiliHistoryPage/SyncResult/SyncRunRecord`
  - 域端点:`HistoryApi`(cursor 历史页)、`DynamicApi`(动态流+客户端分类)、`ToviewApi`(稍后再看全量+PGC 形态)、`BangumiApi`(追番追剧+剧集详情)、`RelationApi`(关注明细/搜索/特别分组)、`SeasonsApi`(合集/系列+其视频列表)、`ArcSearchApi`(投稿+分区索引, WBI)、`ArticleApi`(专栏文集)、`VideoZoneApi`+`VideoZones`(分区补查+静态 v1/v2 表, 自 C# 版机械转换)、`HeartbeatApi`(进度上报 fire-and-forget)
- 播放域端点(pagelist/playurl/PGC playurl/弹幕下载)**不在本变更**,随播放窗口变更迁移(spec 中播放域豁免口径)。
- 无规格 delta:仅实现既有 `bilibili-api-client` 规格,行为契约不变。
- 线程模型说明:管线内部以"专用网络线程 + 阻塞投递"实现跨线程安全调用。

## Impact

- Affected specs: 无修改(实现 `bilibili-api-client`)
- Affected code: 新增 `app/core/*`;app/CMakeLists 注册新文件;新增 `tools/api-probe` 无头探针目标(构建期不参与交付,供手测/联调)
