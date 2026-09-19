# bililocal 弹幕渲染重构评估（2026-09-18）

> 此文保留重构前的诊断与微基准；用户后续已授权实施，当前结果见 [场景图重构交付](弹幕场景图重构与手测.md)。

结论：**值得按“布局与呈现分离、文字复用、逐帧只移动节点”的思路重构。优先使用 Qt 6.11 的公开场景图文字节点，不直接移植 bililocal 的旧 OpenGL 后端。**

本次完成源码对照、离屏微基准及 OpenSpec 设计；未修改生产弹幕与截图实现，未做 UI 自动化。外部参考 `/Users/ziyu/Documents/b3/bililocal` 只读。用户确认“包含弹幕”的截帧入口是播放器顶部截图按钮。

## 当前问题与证据

### 绘制开销

`app/player/DanmakuEngine.cpp` 的 `paint()` 在弹幕入场时缓存 `QPainterPath`，但每帧仍调用 `drawPath()` 重新描边、填充全部在屏文字。缓存路径省去了轮廓构造，并没有省掉光栅化。

`QQuickPaintedItem` 未指定 FBO target，使用默认图片绘制路径；`update()` 未限定脏矩形，每帧整层刷新后交给 Qt Quick 合成。即便改成缓存图片，在这个容器里仍有整层重绘/纹理更新开销，因此图片缓存适合作为过渡，不是最终高密度方案。

另外，16ms QTimer 与屏幕刷新节奏独立；每次 `setPlayback()` 还会重新启动定时器并请求更新。播放器每 100ms 校正媒体时钟，这种重复启动可能带来额外时序波动，需要在真实帧时间分析中量化；不将它直接认定为唯一卡顿根因。

### 字形和平滑度

- 当前没有指定字体族，两处度量/绘制分别依赖默认 QFont 和 painter 字体，无法保证 Windows 微软雅黑或 macOS 苹方。
- 当前强制粗体，再加 2.2px 轮廓描边；小号中文容易显得粗、糊、拥挤。
- `addText()` 转成轮廓再绘制，不等同于正常文本渲染路径；即使启用 TextAntialiasing，也不能恢复正常字形栅格化的全部表现。
- 本机查询默认主字体为 `.AppleSystemUIFont`，`PingFang SC` 可准确解析。默认主字体名不代表每个中文字符最终选用的 fallback，不能据此断言当前中文完全没用苹方。
- 当前没有自行管理字体缓存的跨屏 DPR 失效。Qt Quick 本身会处理部分 DPR，不应仅因源码没设置 textureSize 就断言“一律按 1x 绘制”；真实清晰度仍须 Retina/Windows 缩放手测。

### 截图

实际链路：`PlayerWindow.qml` 顶部按钮 → `PlayerController::screenshot()` → `screenshot-to-file <path> subtitles`。

弹幕是视频的独立 QML sibling，源码没有 grabWindow/grabToImage，也没有把该层送入 mpv。因此当前代码**不能直接解释**用户看到的弹幕进入截图现象，问题保留待复现。`subtitles` 确实允许 mpv 字幕进入输出；`video` 才是明确的纯视频帧语义（已核对本机 mpv 手册）。更改此参数可修正截图契约，但不能在复现前声称它必然修复独立 QML 弹幕混入。

建议后续使用可识别的测试弹幕，在暂停的同一视频帧上分别开/关弹幕、触发顶部按钮，比较保存文件并确认实际运行产物；同时区分客户端弹幕、mpv 字幕与视频源内已烧录文字。视频源自带文字无法靠截图选项去掉。

截图另有两个独立缺陷：命名精度只有秒，同秒截图会覆盖，文件存在检查可能误判旧文件为成功；同步 command 调用在 GUI 线程上，PNG 编码/落盘可能阻塞。后续用唯一文件名、异步命令及完成结果处理，替代固定 300ms 检查。

## 离屏微基准

可复现源：`tools/danmaku_paint_bench.cpp`，可选 CMake 目标 `danmaku-paint-bench`，不参与常规构建或有阈值的 CTest。

配置：本机 macOS、Qt 6.11.2、Release 优化；1920×1080 逻辑画布；32 条相同的 25px 粗体中英混合文字；2.2px 描边；20 帧预热、200 帧计时。两种路径使用同一文字轮廓、位置与清屏操作，区别仅在于每帧重画路径还是复用预生成图片。DPR 2 对应 3840×2160 物理像素。

| 路径 | DPR 1 中位 / P95 | DPR 2 中位 / P95 |
|---|---:|---:|
| 缓存轮廓后逐帧描边填充 | 65.18 / 65.81 ms | 205.30 / 215.70 ms |
| 预生成图片后逐帧合成 | 0.26 / 0.29 ms | 0.96 / 1.05 ms |

这是独立 CPU 光栅化对照，**不是生产播放器测得的帧耗时，也不是 QSG 的预计性能**。未计入缓存生成、车道、mpv、GPU 上传、Qt Quick 合成和实际弹幕分布。它足以支持“重复文字光栅化值得移出逐帧路径”，不能据此承诺几百倍整机加速。

```sh
/Users/ziyu/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target danmaku-paint-bench -j 6
QT_QPA_PLATFORM=offscreen ./build/bin/danmaku-paint-bench
```

## bililocal 哪些值得借鉴

以下路径相对 `/Users/ziyu/Documents/b3/bililocal`。

| 实现 | 证据 | 取舍 |
|---|---|---|
| 准备与绘制分离 | `src/Render/ASprite.h`、`src/Graphic/Plain.cpp` | 保留调度模型、文字资源、绘制节点三个职责 |
| 图片缓存 | `src/Render/Raster/AsyncRasterSprite.cpp` | prepare 时文字和效果合成一次，draw 时只移动图片 |
| 纹理图集 | `src/Render/OpenGL/OpenGLAtlas.cpp` | 按文本/字体/效果缓存整段文字，Alpha8 上传到 2048 图集；它不是 SDF 字形缓存 |
| 批量提交 | `src/Render/OpenGL/OpenGLRender.cpp` | 合并连续同纹理绘制，复用资源；避免随意重排透明元素 |
| 高 DPI | `src/Render/OpenGL/SyncTextureSprite.cpp` | 按实际像素准备资源，映射回逻辑尺寸；新实现应使用窗口 DPR，跨屏重建 |
| 准备队列与在屏列表 | `src/Model/Running.cpp` | 只呈现活跃弹幕；有界预准备可降低入场峰值 |

不照搬：Qt 5 的 beforeRendering/default FBO 接入、直接 OpenGL 状态操作、私有 `qt_blurImage`、seek/clear 时共享线程池 `waitForDone()`、整批 cache 清空策略。`SyncTextureSprite::prepare()` 是空函数，OpenGL 首次 draw 仍可能同步生成文字，不能误认为旧实现已经完全异步化。

旧字体默认设置还包含 macOS 华文黑体，不符合此次苹方目标。借鉴架构不需要复制旧默认值。

## 建议的 Qt 6 路线

1. **保持接口和时钟**：保留 PlayerController、danmakuEnabled 和样式属性、媒体时间插值、暂停/seek/倍速、三种弹幕模式。把调度/车道从 paint() 拆出，以在屏快照驱动呈现。
2. **首选公开文字节点**：将 QQuickPaintedItem 改为 QQuickItem，使用 `QQuickWindow::createTextNode()`、`QSGTextNode` 和 `QTextLayout`。本机 Qt 6.11.2 头文件确认提供这些 API；文字入场或样式变化时构建节点，逐帧只更新 transform/opacity 和增删在屏项。先评估 QtRendering 与 NativeRendering 的观感/性能，避免每帧 clear/addTextLayout。
3. **平台字体**：Windows 首选 `Microsoft YaHei`，macOS 首选 `PingFang SC`，明确 fallback；度量与绘制共用同一 QFont。先对照 Medium/DemiBold 与较细描边，由手测定观感，保留彩色/混合字符正确性。
4. **缓存与生命周期**：场景图节点/纹理只能在相应渲染线程创建释放；GUI 线程准备稳定快照。若文字准备仍是瓶颈，再引入有容量上限的 QImage 预生成队列和 QSGImageNode 路线；使用 generation 丢弃 seek、切片、字体/DPR 改变后的旧结果。跨屏、sceneGraphInvalidated、窗口重建必须验证。
5. **刷新节奏**：用 Qt Quick 帧节奏驱动，位置仍从媒体时钟计算，避免累加帧位移。暂停、不可见、禁用或无活跃项时节流；无活跃项但有即将入场内容时保留下一入场唤醒，不能永久停更。
6. **截图独立修复**：顶部按钮明确请求纯视频，异步保存并准确报告结果；不靠隐藏弹幕等一帧再截图，避免闪烁和竞态。

QSGTextNode 能复用 Qt 的文字设施，通常可减少自研图集、着色器与跨后端维护成本。自建图集作为实测后的备选，不作为第一步前提。既有 mpv 仍使用 OpenGL；采用 QSG 公共 API 不等于本次顺带把播放器迁移到了 Metal/RHI。

## 后续验收

- 对比同一密集弹幕样本：1080p/4K、DPR 1/2、60/120Hz、暂停/倍速/拖动；记录帧时间 P50/P95/P99、CPU、内存、入场峰值，不能只看平均 FPS。
- 验证当前计时、基线、不透明度、车道/尺寸、seek、切片等回归；现有直接调用 paint() 的测试应迁移至模型/文字资源，不保留旧架构作为测试前提。
- 人工比较微软雅黑/苹方、细描边、混合字符、跨屏缩放；正文与描边边缘无明显毛刺，位置变化不产生跳字。
- 在同一暂停帧上以顶部按钮对照开/关弹幕，截图像素一致；反复同秒截图、失败路径和截图时播放流畅性。
- 最终性能目标按真实基线确定，预期目标是在 60Hz/120Hz 下分别满足 16.7ms/8.3ms 整帧预算，不能把该目标写成已达到。

## 本次验证与交付

Release 主程序与可选基准目标构建通过，既有 CTest 4/4 通过，OpenSpec 严格验证及 diff 空白检查通过。基准临时目录已清理，可复现源保留在 tools 中。生产重构任务在 OpenSpec 中明确保持未完成。
