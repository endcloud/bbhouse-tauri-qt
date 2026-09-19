# Bilibili API 本地目录索引与核对记录

更新时间：2026-09-17。供后续开发持久查找；这是仓库内索引，不依赖对话记忆。

## 参考来源与查找方法

本机文档根目录为 `/Users/ziyu/Documents/b3/bilibili-API-collect/docs/`。
项目中的 `b3的替身` 是 Finder alias，不是可供 shell 直接穿透的符号链接。
下表文档路径均相对此根目录；接口实现位于 `app/core/`。

```sh
rg -n 'x/player/wbi/playurl' /Users/ziyu/Documents/b3/bilibili-API-collect/docs
rg -n 'epid|cid|oid' /Users/ziyu/Documents/b3/bilibili-API-collect/docs/historytoview/history.md
rg -n 'Endpoint|api.get|api.postForm' app/core
```

## 与本项目相关的端点

除特别注明外，主机为 `https://api.bilibili.com`；HTTP/JSON 信封、Cookie、UA、Referer、Origin 均由 `BilibiliApiClient` 统一处理。

| 功能 | 文档路径 | 端点 / 核心参数 | 实现 | 核对结果与风险 |
|---|---|---|---|---|
| 导航 / 登录 / WBI 密钥 | `login/login_info.md`、`misc/sign/wbi.md` | `/x/web-interface/nav` | `WbiSigner.cpp`、`PlayerApi.cpp` | 固定置换表正确；修复 `!'()*` 应过滤而非保留、畸形 key 长度检查。无登录 nav 可返回 `-101` 同时携带 key，但本应用现有契约要求有效登录，仍保留统一层登录失效映射。 |
| 历史游标 | `historytoview/history.md` | `/x/web-interface/history/cursor`；`max/view_at/business/ps` | `HistoryApi.cpp` | 路径与游标正确；`history.oid` 是 av，`history.epid` 是分集，`history.cid` 是观看分 P，不能将 oid 用作 epid。 |
| 稍后再看 | `historytoview/toview.md` | `/x/v2/history/toview` | `ToviewApi.cpp` | 全量列表、负进度保留正确；PGC 从 `redirect_url` 提取 ep，保留原始 JSON 供播放入口恢复 cid。 |
| 动态 | `dynamic/all.md`、`dynamic/content.md`、`dynamic/dynamic_enum.md` | `/x/polymer/web-dynamic/v1/feed/all`；`offset` | `DynamicApi.cpp` | 分页、转发取原动态主体与 archive aid 解析符合文档；PGC 可仅有 ep ID，不能要求 cid 才允许起播。 |
| 视频详情 / 分区 | `video/info.md`、`video/video_zone.md`、`video/video_zone_v2.md` | `/x/web-interface/view`；`aid` 或 `bvid` | `VideoZoneApi.cpp`、`VideoZones.cpp` | 路径/参数正确；缺名称由静态表兜底，`-404` 是内容定位失败或失效。 |
| 视频分 P | `video/info.md` | `/x/player/pagelist`；`aid` | `PlayerApi.cpp` | 端点存在且公开样本成功；首 P 兜底仅适用于没有历史 cid 的条目。原代码注释引用不存在的 `video/pagelist.md`，已修正。 |
| UGC DASH / MP4 | `video/videostream_url.md` | `/x/player/wbi/playurl`；`avid/cid/qn/fnval/fnver/fourk` + WBI | `PlayerApi.cpp` | 路径、avid、fnval=4048/1 正确，不能将 avid 改成 aid。补齐 `base_url`，处理仅备用地址和 MP4 `backup_url` 数组，过滤无效流地址。 |
| PGC 播放 | `bangumi/videostream_url.md` | `/pgc/player/web/playurl`；`ep_id` 或 `cid`，载荷 `result` | `PlayerApi.cpp` | 正确使用 result；缺失定位参数应省略，不能传 ep_id=0。`has_paid=false` 本身不表示无权观看，公开非试看样本也为 false；试看依据 is_preview，接口错误仍按原样拒绝。 |
| PGC 详情 / cid 换算 | `bangumi/info.md` | `/pgc/view/web/season`；`season_id` 或 `ep_id` | `BangumiApi.cpp`、`PlayerApi.cpp` | `episodes[].id` 对应 ep_id；修复 PlayerApi 错读 `ep_id` 导致弹幕 cid 丢失，保留旧字段兼容。 |
| 追番 / 追剧 | `bangumi/follow.md` | `/x/space/bangumi/follow/list`；`vmid/type/pn/ps` | `BangumiApi.cpp` | 两桶 type=1/2；港澳台标签直连扫描 type=1 全页后筛标题，本地分页；普通四档保留服务端分页。区域详情与 PGC playurl 使用独立代理客户端。 |
| 字幕 | `video/player.md` | `/x/player/wbi/v2`；`aid/cid` + WBI，字幕 JSON URL | `PlayerApi.cpp` | 参数正确、协议相对 URL 已补 https；尚需有字幕的登录样本手测。 |
| XML 弹幕 | `danmaku/danmaku_xml.md` | `https://comment.bilibili.com/{cid}.xml` | `PlayerApi.cpp`、`DanmakuParser.cpp` | XML 存档不是全历史全量弹幕；gzip/zlib/raw 解压路径存在，画面性能由播放器单独审查。 |
| 关注 / 搜索 / 特别关注 | `user/relation.md` | `/x/relation/followings`、`/x/relation/followings/search`、`/x/relation/tag?tagid=-10` | `RelationApi.cpp` | 参数及响应对象与文档一致；搜索最多 5 页由控制器限制；头像 face 按 https 归一。 |
| 用户名片 | `user/info.md` | `/x/web-interface/card`；`mid`；公开 GET，无 WBI | `UserProfileApi.cpp` | 空间昵称、头像、签名、投稿总数；不传 Cookie；缺失/不匹配 UID 视为错误，保留入口资料。 |
| 投稿 | `user/space.md` | `/x/space/wbi/arc/search`；`mid/order/tid/pn/ps` + WBI | `ArcSearchApi.cpp` | WBI 与分页正确；服务端风控 API `-352` / HTTP `412` 不能伪装成空列表或不断重试。 |
| 合集 / 系列 | `video/collection.md` | `/x/polymer/web-space/seasons_series_list`、`seasons_archives_list`、`/x/series/archives` | `SeasonsApi.cpp` | 正确区分 page_num/page_size 与 pn/ps；列表 page_size 最大 20 来自既有规格实测，保留。 |
| 专栏文集 | `user/space.md`（不是 `article/articles.md`） | `/x/article/up/lists`；`mid` | `ArticleApi.cpp` | 一次全量与 lists 字段正确；`article/articles.md` 是文集内文章列表。 |
| 播放心跳 | `video/report.md` | POST `/x/click-interface/web/heartbeat`；`aid` 或 `epid`，`cid/csrf/type/play_type` | `HeartbeatApi.cpp` | 表单/CSRF 与 UGC type=3、PGC type=4 符合文档；本次未发送写请求，也未验证远程观看记录更新。 |

## 直播接口（2026-09-18）

- `live/follow_up_live.md` → `https://api.live.bilibili.com/xlive/web-ucenter/user/following`，Cookie，`page/page_size=10/ignoreRecord=1/hit_ab=true`。服务端包含未开播主播，按 `totalPage` 续载后筛 `live_status=1`，空在线页不是末页。
- `live/info.md` → `/xlive/web-room/v2/index/getRoomPlayInfo`，`room_id/qn/platform=web/protocol=0,1/format=0,1,2/codec=0,1`。解析 `stream/format/codec/url_info`，只保留同一实际清晰度候选。
- 实现 `LiveApi.*`，两接口都使用现有显式直连管线；媒体使用独立 libmpv 直播会话。参考和验证见 [直播调研](直播接口与内核调研.md) 与 [交付](直播页面与播放手测.md)。

## 流行接口（2026-09-18）

- `video_ranking/popular.md` → `/x/web-interface/popular`（pn/ps、no_more）、`popular/series/list`（实际期数目录）、`popular/series/one?number=N`（校验 config.number）；`video_ranking/precious_videos.md` → `popular/precious`（完整精选列表）。
- `video_ranking/ranking.md` → `/x/web-interface/ranking/v2?rid=N&type=all`。番剧/国创/纪录片/电影/电视剧不能假装 UGC，改用 `/pgc/season/rank/web/list?season_type=1|4|3|2|5&day=3`；文档未收录部分以 wiliwili `source/api/home_api.cpp`、`include/api/bilibili/api.h` 及公开响应核对。仅含 seasonId 时先取整季详情再播放。
- `audio/rank.md` → `/x/copyright-music-publicity/toplist/all_period?list_type=1`、`toplist/music_list?list_id=N`；跨年份排序发布时间，注意期号字段 `priod`，creation/MV 字段整组选取。
- 实现 `PopularApi.*`，共享直连客户端；热门/选期/必刷有 Cookie 时 WBI，匿名不经过登录 nav；目录/榜单/音乐普通 GET。卡片 ID 字符串，播放量使用 playCount，不复用个人 viewCount。
- 第 1 期/第 391 期带凭据和 WBI 返回正确期号；匿名选期/UGC 榜本次出现 -352，需展示真实错误。详见 [流行交付与手测](流行页面与每周必看选期.md)。

## 全文档目录导航

| 范畴 | 目录 |
|---|---|
| 认证与签名 | `login/`、`misc/`（含 `sign/`）、`clientinfo/` |
| 用户与内容消费 | `user/`、`video/`、`video_ranking/`、`bangumi/`、`historytoview/`、`fav/`、`search/` |
| 动态与创作 | `dynamic/`、`opus/`、`article/`、`album/`、`creativecenter/`、`note/` |
| 弹幕与互动 | `danmaku/`、`comment/`、`emoji/`、`broadcast/`、`message/` |
| 其他媒体 | `live/`、`audio/`、`manga/`、`cheese/` |
| 权益与社区 | `vip/`、`electric/`、`wallet/`、`garb/`、`activity/`、`blackroom/`、`teenager/`、`newbie_exam/`、`customerservice/` |
| 网站与客户端部件 | `web_widget/`、`APP_widget/` |

## 本次验证与边界

2026-09-17，使用最少公开 GET、无 Cookie、无写操作得到：

- `pagelist?aid=170001`：HTTP 200 / code=0，10 个分 P。
- 按该结果 cid 和 nav WBI 密钥签名的 UGC playurl（带匿名所需 `gaia_source=view-card`）：HTTP 200 / code=0，3 条 DASH video。
- `pgc/view/web/season?ep_id=85046`：HTTP 200 / code=0，episodes 使用 `id`。
- `pgc/player/web/playurl?ep_id=85046&fnval=4048...`：HTTP 200 / code=0，6 条 DASH video、`is_preview=0`、`has_paid=false`，证明不能以未付费字段单独拒播。
- 已失效样本 `ep_id=6751`：HTTP 200 / API code=-404。由此只能证明该 ID 当前失败，不能推断整个 PGC 端点下线。

这些只验证公开端点和响应形态；未代替账号 Cookie、会员权益、地区限制、Windows 运行时及真实 CDN 音画播放手测。未存储 Cookie、签名播放 URL 或用户观看记录。原 `api-probe` 仍是登录 nav + 历史读取；离线 `api-regression-test` 覆盖文档固定 WBI 向量、URL 字段变体、CDN 排序、64 位 ID、PGC cid 和错误映射。

排查“404”时先区分 HTTP 404、JSON code=-404 与 mpv CDN 404，再查实际 aid/ep_id/cid 是否被 QML int 截断、PGC 是否误走 UGC、签名播放 URL 是否过期。不要把所有失败都归因于网络，也不要无条件回退到旧端点。

## 港澳台用例（2026-09-18）

`bangumi/info.md` 的 `/pgc/review/user?media_id=28233436` 定位 season 37757。用户将本机 Clash 调整为全局转发后，`/pgc/view/web/season` 与 `/pgc/player/web/playurl` 通过 HTTP localhost:7897 成功；实际 Qt 客户端解析 16 集/15 视频候选/3 音频候选且非试看。返回音视频地址各用显式直连 Range 0–1023 验证 HTTP 206。未记录 Cookie、签名地址或观看记录；细节见 [交付说明](港澳台番剧与代理设置.md)。

## 本地历史同步（2026-09-18）

`HistorySyncRunner` 复用 `historytoview/history.md` 的 cursor API，`ps=30/type=all`，页间至少 1s，传回实际 `max/view_at/business`；空 data/list 正常结束，循环或非空页无效游标明确失败且保留已提交页。SQLite 按 `(video_key, view_at)` 追加幂等事件，不把同步时间当观看时间、不覆盖旧事件；手动和原生定时任务共用实现与同库锁。平台与手测见[本地历史与原生定时服务](本地历史与原生定时服务.md)。

## Web 登录与 CC 字幕（2026-09-19）

- 登录参考 `login/login_action/QR.md` 的 Web 二维码生成 `/x/passport-login/web/qrcode/generate` 和状态 `/x/passport-login/web/qrcode/poll`（passport.bilibili.com），状态 86101 等待扫描、86090 等待确认、86038 过期、0 成功；2 秒间隔，180 秒本地截止，刷新/关闭使旧响应失效。
- 扫码成功优先读取 Set-Cookie，旧式返回 URL 仅提取允许的凭据字段，不访问该 URL。导入与扫码均用 `/x/web-interface/nav` 验证登录、匹配 UID，成功后原子写入 `AppPaths::cookiePath()`；不输出 Cookie/二维码密钥，不改全局代理。
- `video/player.md` 的 `/x/player/wbi/v2` 按 aid/cid（PGC 带 ep_id）读取所有有效字幕 URL，不过滤 `ai-*`/`AI`/`ai_type` 轨道。仅 epId 的播放条目可从 season 信息补 aid/cid。在线字幕 JSON 只在内存中按 from/to/content 对齐，不落地签名 URL；字幕访问显式直连。
- 验证使用固定离线响应与本地媒体，真实 Web 扫码和登录账号的字幕 API 由用户手测。详见[交付说明](登录初始化与关于及CC字幕.md)。
