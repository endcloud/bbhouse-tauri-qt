# bilibili-api-client Specification

## Purpose
定义 B 站 API 统一请求层的契约：公共请求管线（鉴权头、响应信封校验、错误映射）、按业务域组织的端点形态，以及后续新端点的接入规范，使散落的 API 调用收敛为一套可扩展架构。

## Requirements

### Requirement: 统一请求管线

B 站 API 请求 MUST 经由统一的客户端层发出：该层 MUST 复用同一 HttpClient 实例，为每个请求注入一致的浏览器 UA、Referer（`https://www.bilibili.com/`）、Origin 与 Cookie 凭据；响应 MUST 先做 `code=0` 信封校验——`code=-101` MUST 抛出登录失效异常（`UnauthorizedAccessException` 语义），其余非零 code MUST 抛出携带 code 与 message 的错误；HTTP 非 2xx MUST 抛出携带状态码的错误。cookie 缺失/为空/无 SESSDATA 的校验 MUST 沿用既有文案与异常类型。

#### Scenario: 登录失效映射

- **WHEN** 任一端点请求返回 `code=-101`
- **THEN** 统一层抛出与既有历史客户端相同语义的登录失效异常，调用方可按类型统一处理

#### Scenario: 头注入一致

- **WHEN** 任一端点发出请求
- **THEN** 请求携带与既有历史客户端一致的 UA/Referer/Origin/Cookie 头，服务端行为不因迁移到统一层而变化

### Requirement: 按域组织端点

统一层 MUST 按业务域组织端点（本变更落地历史域：cursor 分页查询，语义与既有 `FetchPageAsync` 一致——游标翻页、条目解析、终止信号保留）；每个端点 MUST 只依赖统一管线提供的基础设施，MUST NOT 各自新建 HttpClient 或散落拼装请求头。

#### Scenario: 历史域端点可用

- **WHEN** 以 cookie 与游标调用历史域端点
- **THEN** 返回与既有 `BilibiliHistoryClient.FetchPageAsync` 同构的分页结果（条目、下一游标、统计口径一致）

### Requirement: 既有调用路径兼容

既有同步链路（`HistorySyncRunner` 与 Windows 服务）MUST 经门面继续使用历史域端点，外部可观察行为（请求形态、解析结果、异常文案）MUST NOT 因本变更改变。

#### Scenario: 同步回归不变

- **WHEN** 手动同步或定时服务执行全量爬取
- **THEN** 落库结果、sync_runs 审计与文案与迁移前一致

### Requirement: 新端点接入规范

后续新增 B 站端点 MUST 先查阅仓库内文档库 `b3/bilibili-api-collect/docs/` 对应业务域文档，再以业务域为单位挂到统一层；MUST NOT 在统一层之外新起散装 HTTP 调用（播放域既有客户端迁移前豁免，迁移后收回）。文档口径与线上行为冲突时（如容量上限、字段缺失），MUST 以线上实测为准并在端点模块注释记录偏差。

#### Scenario: 新域接入示例

- **WHEN** 后续变更引入一个新的业务域端点（如收藏夹）
- **THEN** 其实现位于统一层的对应域模块，复用统一管线的头注入与信封校验，不重复造请求基建

### Requirement: PGC 播放地址解析端点

API 层 SHALL 提供 PGC（番剧/影视）播放地址解析 wrap：请求 `GET https://api.bilibili.com/pgc/player/web/playurl`，MUST 携带用户 Cookie 与主站一致的浏览器头（UA/Referer），定位参数按接口文档口径 **ep_id 与 cid 任选其一**——历史条目同传两者（取自条目自带数据，MUST NOT 要求先经分 P 列表查询换算），动态条目仅携带 ep_id（cid 缺省时 MUST 省略该参数，MUST NOT 传 0）；qn/fnval/fnver/fourk 语义 MUST 与主站播放地址接口一致（DASH 路径沿用同一 fnval 组合值，MP4 durl 路径 fnval=1）。响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code 抛出携带 code 与 message 的错误——但数据体 SHALL 取自 `result` 字段（与主站 `data` 信封不同）。解析 SHALL 产出与主站路径同构的结果：DASH 路径的 video/audio 段候选链（含 CDN 排序口径）与含 need_vip 的档位列表、MP4 路径的 durl 直链；并 SHALL 提取权益字段 is_preview、has_paid 与 vip_type 供上层做权益判定。实现 MUST 落在播放域端点模块内复用主站解析设施，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: DASH 解析同构

- **WHEN** 以 fnval 的 DASH 组合值请求某番剧分集的 PGC 播放地址且账号有权观看
- **THEN** 解析结果给出与主站路径同构的视频/音频段候选链与档位列表（含 need_vip 标记），选流与 CDN 排序口径一致

#### Scenario: 仅 ep_id 定位

- **WHEN** 以动态条目携带的 ep_id（无 cid）请求 PGC 播放地址（cid 参数省略）
- **THEN** 接口按 ep_id 单独定位分集并正常返回 DASH/durl 载荷与权益字段，解析结果与同传 cid 时同构

#### Scenario: result 信封与错误映射

- **WHEN** PGC 接口返回 code=0（数据在 result）或 code=-101 或其他非零 code
- **THEN** 分别按成功解析、登录失效异常、携带 code 与 message 的错误处理，与统一请求层口径一致

#### Scenario: 权益字段提取

- **WHEN** 某会员番剧对非大会员账号返回试看语义（is_preview=1、has_paid=false）
- **THEN** 解析结果携带这些权益字段原值，供上层拒播判定使用

#### Scenario: MP4 durl 路径可用

- **WHEN** DASH 路径解析失败后以 fnval=1 请求同一分集
- **THEN** 解析产出 durl 单流直链与可用清晰度列表，供回落管线起播
### Requirement: 动态流列表端点

API 层 SHALL 提供动态流列表端点：请求 `GET https://api.bilibili.com/x/polymer/web-dynamic/v1/feed/all`，MUST 经统一管线携带用户 Cookie（SESSDATA）与主站一致的浏览器头发送，MUST NOT 要求 WBI 签名；分页 MUST 以 `offset`（上一响应末条动态 id）推进并 MUST 暴露"是否还有更多"终止信号。响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code 抛出携带 code 与 message 的错误。解析 SHALL 产出每条动态的内容分类（视频/番剧影视/专栏/其他，按动态主体类型判定，转发取原动态主体归类）与卡片展示字段：动态 id、标题或文字摘要、封面图、作者名、发布时间戳、时长、av 号（视频类）、跳转链接；条目 MUST 携带原始 JSON 供上层扩展。实现 MUST 落在统一层的动态域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 首页与翻页

- **WHEN** 以 cookie 调用端点（首次不带 offset，续拉携带上一响应返回的 offset）
- **THEN** 返回动态条目列表与新的 offset/终止信号，两页条目按动态 id 不重叠

#### Scenario: 错误映射一致

- **WHEN** 接口返回 `code=-101` 或其他非零 code
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致

#### Scenario: 分类与字段解析

- **WHEN** 响应包含投稿视频、剧集、专栏、图文、转发等不同主体类型的动态
- **THEN** 每条产出对应分类与展示字段（视频类含 av 号与时长文本、图文类取首图为封面并产出文字摘要、转发按原动态主体归类并沿用原动态封面/标题）

### Requirement: 稍后再看列表端点

API 层 SHALL 提供稍后再看列表端点：请求 `GET https://api.bilibili.com/x/v2/history/toview`，MUST 经统一管线携带用户 Cookie（SESSDATA）与主站一致的浏览器头发送，MUST NOT 要求 WBI 签名，MUST NOT 携带分页参数——端点单次返回**全量列表快照**（条目数以服务端下发为准，客户端 MUST NOT 假定固定上限）。响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code 抛出携带 code 与 message 的错误。解析 SHALL 产出与历史域条目同构的卡片字段：avid/bvid、标题、封面图、UP 主名称与 mid、总时长（秒）、分 P cid、观看进度（秒，负值 SHALL 保留原值并按"已看完"语义供上层隐藏进度）、添加时间戳（条目排序键，列表 MUST 按其降序）；稿件失效状态（state<0）SHALL 被解析供上层占位处理。PGC 形态条目（番剧/电影/综艺）SHALL 被识别：判定依据为影视类标注字段非空，且 MUST 从其分集页跳转地址（`bangumi/play/ep<id>`）提取 epid——此类条目自带分集 cid（与历史页 PGC 条目同构成对定位）。每条条目 MUST 携带原始 JSON 供上层 PGC 定位与扩展。实现 MUST 落在统一层的稍后再看域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 全量单次返回

- **WHEN** 以 cookie 调用稍后再看列表端点（账号列表非空）
- **THEN** 单次请求返回全量条目列表（含总数字段与条目数组，二者计数一致），条目按添加时间降序，无需第二次请求即可获得完整列表

#### Scenario: PGC 形态识别

- **WHEN** 响应包含番剧/电影/综艺类条目（影视类标注非空且带分集页跳转地址）
- **THEN** 解析产出该条目的 epid（自跳转地址提取）与既有 UGC 字段同构的展示字段，条目原始 JSON 完整保留

#### Scenario: 失效与已看完字段

- **WHEN** 响应包含失效稿件（state<0）与已看完条目（进度为负值）
- **THEN** 解析保留失效状态供上层占位处理；进度原值保留（负值不裁剪为 0），供上层按已看完语义处理

#### Scenario: 错误映射一致

- **WHEN** 接口返回 `code=-101` 或其他非零 code
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致

### Requirement: 视频分区信息端点与静态分区表

API 层 SHALL 提供视频分区信息端点：请求 `GET https://api.bilibili.com/x/web-interface/view`，以 bvid（或 aid）定位单个视频，MUST 经统一管线携带用户 Cookie 与主站一致的浏览器头，MUST NOT 要求 WBI 签名。解析 SHALL 产出分区定位字段：`tid`、`tname`、`tid_v2`、`tname_v2` 的原值（线上现况 `tname`/`tname_v2` 恒为空串，MUST 保留原值、MUST NOT 臆造名称）及视频标题。响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code（含稿件失效的 `-404`）抛出携带 code 与 message 的错误。

API 层 SHALL 另内置静态分区映射表（源数据为仓库文档库 b3/bilibili-api-collect 的 `docs/video/video_zone.md` v1 表与 `docs/video/video_zone_v2.md` v2 表），提供"分区号→分区名称"与"分区号→所属主分区（名称与分区号）"的纯本地查询：tid 命中 v1 表为主；未命中时以 `tid_v2` 查 v2 表兜底；两处均未命中或无分区号 MUST 归"未知分区"语义。分区表 MUST 以代码内静态数据承载，MUST NOT 在运行期联网拉取。实现 MUST 落在统一层的对应域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 分区字段解析

- **WHEN** 以动态流视频条目携带的 bvid 调用端点（视频可公开访问）
- **THEN** 解析产出数字 `tid`（及 `tid_v2`）与 `tname`/`tname_v2` 原值（空串保留为空串）和视频标题

#### Scenario: 错误映射一致

- **WHEN** 接口返回 `code=-101`（登录失效）、`code=-404`（稿件失效）或其他非零 code
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致

#### Scenario: 静态表主兜底映射

- **WHEN** 以 tid=28 查询静态表，以及以 v1 表未收录的 tid 配其 `tid_v2=2045` 查询
- **THEN** 前者得出子分区"原创音乐"、主分区"音乐"；后者经 v2 表兜底得出"动漫资讯"、主分区"动画"

#### Scenario: 未知分区语义

- **WHEN** 视频未解析出分区号，或分区号在 v1/v2 两表均未命中
- **THEN** 查询结果归"未知分区"语义，MUST NOT 抛错

#### Scenario: 免 WBI 与管线复用

- **WHEN** 端点发起请求
- **THEN** 不携带 WBI 签名参数，复用统一层的头注入与信封校验，不重复造请求基建

### Requirement: 关注关系端点

API 层 SHALL 提供关注关系域端点（挂统一管线、Cookie 认证、MUST NOT 要求 WBI 签名）：

- **关注明细** `GET https://api.bilibili.com/x/relation/followings`：以 vmid（当前账号 mid）、ps/pn 分页查询关注列表；解析 SHALL 产出每成员的 mid、昵称、头像 URL、签名与特别关注标志，并暴露总数字段供翻页上限计算；MUST 沿用统一管线既有浏览器 UA 与 Referer 头（该端点对非浏览器头有风控拦截）。
- **关注搜索** `GET https://api.bilibili.com/x/relation/followings/search`：以 vmid + name 关键词 + ps/pn 分页查询关注列表的昵称匹配；响应 list 与关注明细同构（复用解析）；分页受服务端 5 页硬上限（超出返回 code 22007，客户端 MUST 限制 pn ≤ 5）。实测口径与文档偏差（2026-09-03 实拉验证）：响应 total 为**匹配总数**（文档标注"关注总数"有误）；省略 name 时返回空列表而非全量（空词场景由调用方走关注明细端点）。
- **特别关注分组明细** `GET https://api.bilibili.com/x/relation/tag?tagid=-10`：ps/pn 分页返回分组内完整用户对象（mid、昵称、头像 URL）；供客户端本地列表的一次性种子导入。

响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code 抛出携带 code 与 message 的错误。实现 MUST 落在统一层的关注关系域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 关注明细分页

- **WHEN** 以 cookie 按 ps=50、pn=1 与 pn=2 调用关注明细端点
- **THEN** 两页成员不重叠，每成员携带 mid/昵称/头像，总数字段一致且可供上限计算

#### Scenario: 关注搜索分页与匹配计数

- **WHEN** 以 cookie 与关键词按 ps=24、pn=1 与 pn=2 调用关注搜索端点且匹配超过单页
- **THEN** 两页匹配不重叠，total 为匹配总数（非关注总数）；pn 始终 ≤ 5

#### Scenario: 空关键词返回空

- **WHEN** 以省略 name 的请求调用关注搜索端点
- **THEN** 响应 list 为空（非全量列表），空词浏览场景由调用方改走关注明细端点

#### Scenario: 特别关注分组明细

- **WHEN** 以 cookie 调用分组明细端点（tagid=-10）
- **THEN** 返回分组成员完整用户对象（含头像与昵称），按 ps/pn 分页推进

#### Scenario: 错误映射一致

- **WHEN** 接口返回 `code=-101` 或其他非零 code（如 -352 风控拦截）
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致
### Requirement: 合集与系列列表端点

API 层 SHALL 提供合集与系列列表端点：请求 `GET https://api.bilibili.com/x/polymer/web-space/seasons_series_list`，MUST 经统一管线携带与主站一致的浏览器头（登录态 Cookie 按既有口径携带；MUST NOT 要求 WBI 签名）；查询 MUST 携带 `mid`，分页以 `page_num`/`page_size` 推进；page_size 服务端上限 20（文档仅标"默认 20"未记上限，超限返回空 data 的 -400——2026-09-07 实测：21/30 拒绝、19/20 正常），端点模块 MUST 对请求页大小做上限收敛。响应 MUST 解析出**合集列表与系列列表两个条目集合**，每条目 SHALL 产出：合集/系列 ID、标题（响应 name 字段）、封面 URL、视频总数、所属 mid；合集条目与系列条目 MUST 可区分（以各自 id 字段名或显式类型标记）。分页推进以两个列表的合计条数对照响应分页信息判定，解析 MUST NOT 假定两个列表在单页内条数比例。实现 MUST 落在统一层的合集域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 双列表解析

- **WHEN** 以 cookie 与某 UP 的 mid 调用端点首页
- **THEN** 解析产出其合集条目集合与系列条目集合（各含 ID/标题/封面/总数），二者可区分且不混淆

#### Scenario: 翻页推进

- **WHEN** 该 UP 的合集与系列合计超过单页 page_size
- **THEN** 以 page_num 递增可拉到全部条目，跨页条目不重叠不丢失

#### Scenario: 无合集与系列的 UP

- **WHEN** 以无任何合集与系列的 UP mid 调用端点
- **THEN** 解析产出两个空集合且信封校验通过（非错误态）
### Requirement: 合集与系列视频列表端点

API 层 SHALL 提供合集与系列的视频分页端点（挂统一管线、MUST NOT 要求 WBI 签名）：

- **合集视频** `GET https://api.bilibili.com/x/polymer/web-space/seasons_archives_list`：以 `mid` + `season_id` 查询，`page_num`/`page_size` 分页；
- **系列视频** `GET https://api.bilibili.com/x/series/archives`：以 `mid` + `series_id` 查询，`pn`/`ps` 分页。

两端点解析 SHALL 产出同构的视频条目：avid/bvid、标题、封面 URL、时长（**合集端点时长字段单位为秒**，解析 MUST 归一为秒）、发布时间戳；分页完成判定 MUST 以"已拉条数对照响应总数字段"为准（响应无显式"是否有下页"字段）。合集播放所需的整套条目 SHALL 可经循环翻页拉全。实现 MUST 落在统一层的合集域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 合集视频拉全

- **WHEN** 以 mid + season_id 循环翻页拉取某 35 集合集（page_size=30）
- **THEN** 两页共拉到 35 条同构条目（bvid/标题/封面/时长秒），条目与合集总数一致，第三次翻页不再产出条目

#### Scenario: 系列视频拉全

- **WHEN** 以 mid + series_id 循环翻页拉取某系列
- **THEN** 与合集端点同构地产出全部条目，分页参数名差异（pn/ps）被端点模块封装

#### Scenario: 时长单位归一

- **WHEN** 合集视频端点响应条目 duration 为秒数值
- **THEN** 解析产出的时长以秒为单位（与历史域条目口径一致），供播放列表构造直接使用

### Requirement: 投稿视频端点

API 层 SHALL 提供投稿视频端点：请求 `GET https://api.bilibili.com/x/space/wbi/arc/search`，MUST 经统一管线携带用户 Cookie 与浏览器头，**MUST 携带 WBI 签名（w_rid/wts）**——签名设施 SHALL 落在统一层（nav 端点取 img_key/sub_key、固定置换表求 mixin key、键进程内缓存），MUST NOT 在统一层外另建签名实现。查询 MUST 支持：`mid`、`order`（pubdate 最新发布/click 最多播放/stow 最多收藏）、`tid`（分区筛选，0=不筛）、`pn`/`ps`（分页，默认 30/页）。解析 SHALL 产出：视频条目（avid/bvid、标题、封面、投稿时间戳、播放量、弹幕数、时长文本 MM:SS 及其秒值）、**分区索引**（tid→分区名+计数，来自响应 tlist，供上层分区筛选零补查）、总投稿数（供分页上限）。实现 MUST 落在统一层的空间域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 首页与分页

- **WHEN** 以 cookie 与 mid 调用端点（pn=1、ps=30，后续 pn=2）
- **THEN** 两页各至多 30 条视频条目且不重叠，总数字段一致供分页栏计算页数

#### Scenario: 排序与分区参数

- **WHEN** 分别以 order=pubdate 与 order=click（及 tid=某分区）调用
- **THEN** 条目分别按发布时间/播放量排序，tid 筛选下条目均属该分区且分区索引仍为全量口径

#### Scenario: WBI 签名

- **WHEN** 调用端点
- **THEN** 请求携带统一层签名设施产出的 w_rid/wts；无签名或签名错误时服务端拒绝（该端点为 WBI 必需）
### Requirement: 专栏文集端点

API 层 SHALL 提供专栏文集端点：请求 `GET https://api.bilibili.com/x/article/up/lists`，MUST 经统一管线携带用户 Cookie（SESSDATA）与浏览器头，MUST NOT 要求 WBI 签名；查询携带 `mid`（与可选排序 0 最近更新/1 最多阅读）。端点一次返回全量文集列表（无服务端分页参数），解析 SHALL 产出每文集：文集 id、名称、封面、最近更新时间戳、总字数、阅读量、包含文章数，及文集总数。实现 MUST 落在统一层的专栏域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 文集全量返回

- **WHEN** 以 cookie 与某有专栏的 UP mid 调用端点
- **THEN** 一次响应返回其全部文集（含名称/封面/文章数/阅读/字数）与总数，客户端本地分页

#### Scenario: 无文集 UP

- **WHEN** 以无专栏文集的 UP mid 调用端点
- **THEN** 解析产出空列表且信封校验通过（非错误态）

### Requirement: 追番追剧列表端点

API 层 SHALL 提供追番追剧列表端点：请求 `GET https://api.bilibili.com/x/space/bangumi/follow/list`，MUST 经统一管线携带用户 Cookie（SESSDATA）与主站一致的浏览器头，MUST NOT 要求 WBI 签名。查询 MUST 携带 `vmid`（当前账号 mid，自 Cookie 的 DedeUserID 解析）、`type` 与 `pn`/`ps` 分页参数；`type` 的服务端值域为 **1（追番）与 2（追剧）**——文档中"1 追番/2 追剧"之外的取值返回 -400（2026-09-07 实测：type=3..6 均拒绝；追番桶内 season_type 番剧与国创混合下发、追剧桶内电影/纪录片/电视剧/综艺混合下发），端点模块 MUST 按此两桶语义建模并保留条目的 `season_type`/`season_type_name` 供上层客户端分类。解析 SHALL 产出每条目：season_id、标题、竖版封面 URL、方形封面 URL、season_type 与类型名、总集数与完结标记、更新展示文本（new_ep.index_show）、评分分值与评分人数（rating 缺省时为空）、观看进度文本（progress 字符串，如"看到第1话"，可为空）、简介（subtitle/evaluate）、跳转地址（ss 链接），并暴露桶内总数字段供全量翻页终止判定；条目 MUST 携带原始 JSON 供上层扩展。响应信封校验 MUST 沿用统一口径——`code=0` 成功、`code=-101` 映射登录失效异常、其余非零 code 抛出携带 code 与 message 的错误。实现 MUST 落在统一层的番剧域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 追番桶分页

- **WHEN** 以 cookie、vmid 与 type=1 按 ps=30、pn=1 起循环调用端点
- **THEN** 各页条目按 season_id 不重叠，条目携带 season_type（番剧与国创混合）与展示字段，累计条数对照总数字段可判定拉全终止

#### Scenario: 追剧桶含混合类型

- **WHEN** 以 type=2 调用端点
- **THEN** 条目携带 season_type（电影/纪录片/电视剧/综艺等混合）原值与类型名，供上层按类型过滤出影视与纪录片档

#### Scenario: 自身 mid 解析

- **WHEN** 端点模块从 cookie 字符串解析当前账号 mid
- **THEN** 从 DedeUserID 提取数字 mid；缺失时抛出登录失效语义错误，MUST NOT 发出无 vmid 请求

#### Scenario: 错误映射一致

- **WHEN** 接口返回 `code=-101` 或其他非零 code
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致

### Requirement: PGC 剧集详情端点

API 层 SHALL 提供 PGC 剧集详情端点：请求 `GET https://api.bilibili.com/pgc/view/web/season`，以 `season_id` 定位剧集（播放域既有客户端的 ep_id 换算形态保留不动），MUST 经统一管线携带用户 Cookie 与主站一致的浏览器头，MUST NOT 要求 WBI 签名。响应数据体位于 `result` 信封（与 PGC 播放地址端点同），解析 SHALL 产出：剧集 season_id 与标题、**分集有序列表**（每集 epid、cid、集标题 title、长标题 long_title、时长毫秒、角标文本），以及**当前账号观看进度**（user_status.progress：last_ep_id、last_ep_index、last_time 秒；无记录时为空）；每条响应 MUST 携带原始 JSON 供上层扩展。信封校验沿用统一口径（`result` 体语义同 PGC 播放地址端点：code=0 成功、-101 登录失效、其余非零抛错）。实现 MUST 落在统一层的番剧域端点模块内，MUST NOT 另起散装 HTTP 客户端。

#### Scenario: 分集列表与 cid

- **WHEN** 以 cookie 与某 season_id 调用端点
- **THEN** 解析产出该剧集全部分集的有序列表（epid/cid/标题/时长齐全，顺序与正片一致），选集起播无需再经其他接口换算 cid

#### Scenario: 观看进度定位

- **WHEN** 该剧集在当前账号下有观看记录（user_status.progress 非空）
- **THEN** 解析产出 last_ep_id 与 last_time，供上层定位续播分集与集内位置；无记录时该字段为空，上层回落第 1 集

#### Scenario: 错误映射一致

- **WHEN** 接口返回 code=-101 或其他非零 code（如地区限制/下架）
- **THEN** 分别按登录失效异常与携带 code/message 的错误处理，与统一请求层口径一致

### Requirement: 播放进度心跳上报端点

统一层 SHALL 提供 B 站 web 端播放进度心跳上报端点封装：`POST https://api.bilibili.com/x/click-interface/web/heartbeat`，`application/x-www-form-urlencoded` 表单正文，认证仅凭 Cookie（SESSDATA），表单携带 `csrf`（Cookie 中 `bili_jct` 的值，缺失时省略该字段）。参数口径：UGC 条目 `aid`+`cid`+`type=3`；PGC 条目 `epid`+`cid`+`type=4`；`played_time` 为播放进度秒（本次会话位置，无则 0）；`realtime` 与 `played_time` 同值（文档自述参数计算为推测，同值口径规避争议）；`start_ts` 为本次上报 UNIX 秒级时间戳；`dt=2`；`play_type` 表达播放动作（1=开始播放、4=结束播放）。请求 SHALL 经统一管线发出（UA/Referer/Origin/Cookie 头注入、`code=0` 信封校验、`-101` 登录失效异常映射）；响应无 `data` 体（`code/message/ttl`）SHALL 视为成功。上报为尽力而为语义：调用方 SHALL fire-and-forget，端点失败 MUST NOT 上抛为播放错误。Core SHALL 提供 `bili_jct` 提取辅助（与 `DedeUserID` 提取同型）。

#### Scenario: UGC 切换上报形态

- **WHEN** 对 aid=123、cid=456 的投稿视频以结束播放动作上报最后位置 300 秒
- **THEN** 请求 POST 至 heartbeat 端点，表单含 `aid=123`、`cid=456`、`type=3`、`played_time=300`、`realtime=300`、`play_type=4`、`dt=2`、`start_ts` 与 `csrf=<bili_jct>`，头含 Cookie/UA/Referer

#### Scenario: PGC 起播上报形态

- **WHEN** 对 epid=5751673、cid=41394834610 的剧集分集以开始播放动作上报起始位置 0 秒
- **THEN** 表单含 `epid=5751673`、`cid=41394834610`、`type=4`、`played_time=0`、`play_type=1`，其余口径同 UGC

#### Scenario: 失败静默

- **WHEN** 上报请求网络失败或信封 `code!=0`
- **THEN** 异常终结在调用点的静默捕获内，播放、切播与任何 UI 状态不受影响

#### Scenario: 信封成功口径

- **WHEN** 响应为 `{"code":0,"message":"0","ttl":1}`（无 data 体）
- **THEN** 端点封装按成功返回，不因缺 data 报错
