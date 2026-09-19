# wiliwili 调研:可移植到 Qt6+QML 的实现清单

> 调研对象:`b3/wiliwili`(原生 C++ 自绘 UI 的 B 站第三方客户端,borealis 框架,GPL-3.0)
> 调研日期:2026-09-17 · 目的:为 bbhouse-qt(Qt6+QML 重实现)寻找可直接移植的 C++ 实现
> 精读:`view/danmaku_core.cpp`(919 行)、`view/mpv_core.cpp`(1408 行);其余经代理扫描

## 0. 总体判断

wiliwili 与本项目同为"B 站 API + mpv + 自绘 UI"形态,其 **与 UI 框架无关的 C++ 代码(签名算法、DASH 选择、弹幕数据结构、解压/格式化工具)价值最高,可近乎原样拷贝**;borealis/NVG 渲染层不值得搬,QML 用 ListView/Loader/PointerHandler 原生替代。

## 1. 可直接拷贝(纯算法,框架无关)

| 内容 | 位置 | 说明 |
| --- | --- | --- |
| WBI 签名 | `api/util/wbi.cpp` | nav 取 img/sub key → MIXIN_KEY_ENC_TAB 重排 32 位 → 参数加 wts、过滤 `!'()*`、字典序拼接 → `w_rid=md5(query+mixin_key)`;缓存 1 小时。与原 WinUI 项目 WbiSigner 同构,C++ 版可互为印证 |
| appkey 签名 | `api/util/http.cpp:19-27` | 参数排序 + md5(query+secret);appkey 表在 `include/api/bilibili/util/http.hpp:20-22`(主要用于 TV/APP 接口,web 接口可不用) |
| DASH 清晰度/音轨回退 | `activity/player_base_activity.cpp:596-709` | 横竖屏限制清晰度 → codecid 选 H264/HEVC/AV1 → 音频 杜比/FLAC/30280/30232/30216 多级回退;主+backup url 一起交给内核 |
| 弹幕 XML p 属性解析 | `view/danmaku_core.cpp:74-158` | `time,type,fontSize(/25),fontColor,level,...`;type 1-3 滚动/4 底/5 顶/7 高级;type==7 高级弹幕动画参数(起止坐标/透明度/路径跟随/缓动)解析完整 |
| gzip/deflate 解压 | `utils/string_helper.cpp:48-82` | zlib `inflateInit2(16+MAX_WBITS)` 循环 inflate——弹幕 list.so(protobuf/gzip)与字幕下载都会用;Qt 的 qUncompress 格式不兼容,照抄此函数 |
| 数字/时间格式化 | `utils/number_helper.cpp` | `sec2Time`(h:mm:ss)、`num2w`(万/亿缩写)、相对时间 |
| seek 步进 | `view/video_view.cpp:40-47` | getSeekRange:5/15/30/60 秒/20% 分档 |

## 2. 改写移植(逻辑保留,载体更换)

- **弹幕引擎核心算法**(`view/danmaku_core.cpp:546-794` `DanmakuCore::draw`):
  - 车道分配:滚动行 `scrollLines[k]=(该行上一条完全进入时刻, 完全离开时刻)`,顶/底行 `centerLines[k]=占用的结束时刻`;新弹幕按 `i.time` 与行占用比较落行,放不下丢弃。
  - 位置基准:滚动弹幕位置 = `speed × (当前CPU时刻 − startTime) × videoSpeed`(绘制帧率平滑),暂停时退化为 `speed × (playbackTime − time)`(暂停弹幕随之暂停);倍速变更时按 `oldSpeed/newSpeed` 反推已有弹幕 startTime,保证位置不跳(`setSpeed`,danmaku_core.cpp:391-404)。
  - 显示行数 = `height/lineHeight × 显示区域%`;过滤链:等级→类型→彩色→失效。
  - **Qt 移植**:数据结构与算法照抄,绘制层换 QQuickPaintedItem+QPainter(描边/阴影两遍绘制:先描边 pass 再正文 pass,同 wiliwili 的 NVG 双 pass)。该算法比 DanmakuFrostMaster(原 WinUI 项目移植源)简洁得多,作为本项目弹幕引擎的首选蓝本。
- **弹幕下载**:`GET /x/v1/dm/list.so?oid={cid}`(XML,gzip)→ QXmlStreamReader 解析 `<d p="...">`(原 tinyxml2 逻辑保留)。
- **CC 字幕**:`view/subtitle_core.cpp` 懒加载 JSON 字幕、二分游标找 `from<=t<=to` 行;渲染改为 QML Text/Rectangle 叠加层。
- **HTTP 层**:cpr → QNetworkAccessManager;注意 wiliwili 手动拼 `k=v; k2=v2` cookie 头(cpr 不符合 RFC6265 的教训)——QNetworkCookieJar 同理建议手动拼,UA 用浏览器形态规避风控。
- **播放链接有效期校验**(`player_base_activity.cpp:534`):110 分钟过期自动暂停+重取;EOF 连播校验 progress 距结尾 5s 内;进度每 15s 上报——三段逻辑照抄,事件总线 `APP_E->fire` 改 Qt signals。

## 3. mpv 集成差异(不搬,但可对照)

wiliwili 用 **render API**(`vo=libmpv`,OpenGL/D3D11 FBO 融入自绘渲染,`mpv_core.cpp:460-530`);本项目当前也使用 **render API + QQuickFramebufferObject**，而非旧 WinUI 的 wid 子窗口方案。最新修复见 `doc/player-runtime-review.md`。可借鉴的细节:
- 观察属性集:`core-idle`/`eof-reached`/`duration`/`playback-time`/`paused-for-cache`/`speed`/`volume`/`pause`/`seeking`/`hwdec-current`(mpv_core.cpp:398-421);
- `setlocale(LC_NUMERIC, "C")` 防小数点本地化坑;`keep-open` + `loop-file=no` + `reset-on-next-file=speed,pause`;`hr-seek=yes`;
- wakeup 回调 → 单线程事件排空模式;
- 失焦自动暂停 + 恢复 120s 窗口(mpv_core.cpp:441-456,可选增强)。

## 4. UI 模式对照(思路参考)

- **RecyclingGrid**(虚拟化网格):固定估计行高 + 可视区±预取行回收复用 + 触底翻页 + 骨架屏——QML 的 ListView/GridView(cacheBuffer + delegate 复用)原生具备,**只移植数据源类的字段映射**。
- **OSD 自动隐藏**:非计时器,每帧比较 `osdLastShowTime = now + OSD_SHOW_TIME`(video_view.cpp:899-906)——与原项目"3 秒无指针隐藏"口径一致。
- **手势状态机**(OsdGestureRecognizer):单击 OSD 开关、双击播放/暂停、长按 2x、横拖 seek(≤120s)、右竖拖音量——QML TapHandler/DragHandler 实现同状态机;触屏需求可作后续增强。
- **视频卡片**:`setCard(封面+尺寸后缀, 标题, UP, 播放, 弹幕, badge)`;封面 CDN 转码后缀 `@672w.webp` 思路与原项目一致。
- **配置持久化**:`unordered_map<SettingItem, value>` → 单 JSON 文件(`config_helper.cpp:901-906`)——Qt 用 QSettings 或同款 JSON 均可。

## 5. 许可注意

wiliwili 为 GPL-3.0。仅移植**算法逻辑**(签名/解析/车道分配等)并自行实现,属思想借鉴;若直接复制代码文本,本项目同样需以 GPL 兼容方式分发。当前仓库为个人使用,与 libmpv(GPL 构建)同策略处理。
