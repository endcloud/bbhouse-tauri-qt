# Tasks: migrate-bilibili-api-client

## 1. 管线与基础设施(自研移植)

- [x] 1.1 `ApiErrors.h`:`ApiError`(code/message/httpStatus)与 `ApiUnauthorizedError` 异常类型;`Loc` zh 中性文案辅助
- [x] 1.2 `JsonHelpers.h`:getString/getInt64/getDouble(NaN=缺席)/getBool/getChild/getChildArray/firstNonEmpty/firstArrayString/firstArrayObjectString/历史宽松与动态严格两种 URL 归一化
- [x] 1.3 `BilibiliApiClient.h/cpp`:QNetworkAccessManager + 专用网络线程 + 阻塞投递;get/postForm/getBytes;信封校验(-101 登录失效;非零 code 抛错;非 2xx 抛错);cookie 三预检(文案沿用)
- [x] 1.4 `AppPaths.h/cpp`:自 exe 目录向上探测仓库根(≤4 级);数据目录 QStandardPaths AppLocalDataLocation;db/export/cookie 路径
- [x] 1.5 `WbiSigner.h/cpp`:nav→img/sub key→置换前 32 位→wts+排序+过滤(!'()*+md5;4h 进程缓存

## 2. 模型与历史域

- [x] 2.1 `HistoryModels.h`:HistoryItem/ViewRecord/HistoryCursor/BilibiliHistoryPage/SyncResult/SyncRunRecord/SyncSource 常量
- [x] 2.2 `HistoryApi.h/cpp`:cursor 请求构造、条目解析(video_key 即产、subtitle 三级兜底、四类 business 链接兜底、宽松封面归一化)

## 3. 其余域端点(参照 C# 原实现机械移植,两代理并行完成)

- [x] 3.1 `DynamicApi`(feed/all,offset 翻页,effective major 分类+条目级 ARTICLE 精判,live_rcmd 二次解析,严格 https 封面)+ `DynamicFeedModels.h`
- [x] 3.2 `ToviewApi`(单次全量、进度负值保留、state<0 失效、PGC 形态 TryParsePgcLocation)
- [x] 3.3 `BangumiApi`(follow/list type∈{1,2} 两桶、vmid 自 DedeUserID、season 详情 episodes/user_status 含 last_ep_index+rawJson 补齐)
- [x] 3.4 `RelationApi`(followings 浏览器头、search pn≤5、tagid=-10 分组)+ `CookieHelpers.h`
- [x] 3.5 `SeasonsApi`(seasons_series_list page_size≤20 收敛、双列表可区分;archives/series 视频列表时长归一秒)
- [x] 3.6 `ArcSearchApi`(WBI 必需、tlist 分区索引、order 三档、MM:SS;412 退避重试×3)
- [x] 3.7 `ArticleApi`(文集全量,mid 单参——与 C# 原文一致,sort 不暴露)
- [x] 3.8 `VideoZones` 静态表(v1 149 条/v2 236 条,自 VideoZones.cs 机械转换)+ `VideoZoneApi`(view 端点 tid/tname 原值)
- [x] 3.9 `HeartbeatApi`(表单口径含 csrf=bili_jct 缺省省略、无 data 体=成功、fire-and-forget)

## 4. 构建与验证

- [x] 4.1 app/CMakeLists 注册全部新文件;全量构建 0 error
- [x] 4.2 `tools/api-probe` 无头探针:实跑通过(cookie 预检 + wbi 签名 + 历史首页 30 条解析 + video_key 形态,PROBE_OK)
- [x] 4.3 openspec validate + 归档 + git commit
