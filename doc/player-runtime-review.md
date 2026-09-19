# 播放内核与弹幕审查（2026-09-17）

2026-09-19 补充：[本地视频黑屏修复](下载管理与本地媒体库.md)。`mpv/render.h` 要求先创建渲染上下文，再加载视频；本地文件立即加载会抢在 QML 挂载前，导致视频轨禁用而音轨继续。点播现通过渲染就绪握手启动候选加载，并隔离旧内核通知；真实视频像素回归补足原静音 WAV 测试的盲区。

参考只读仓库 `wiliwili/wiliwili/source/view/mpv_core.cpp`、`danmaku_core.cpp`，并以项目自带 `mpv/client.h`、`render.h` 的 ABI 和线程约束为准。

## 已修复

- **属性 ABI 错误**：原 `FLAG=0`、`DOUBLE=100` 并非 libmpv 的格式枚举，导致暂停、时长、可定位和缓存状态失效。现在直接使用官方 `MPV_FORMAT_FLAG`、`MPV_FORMAT_DOUBLE`，函数指针参数也与头文件一致。
- **加载诊断不完整**：macOS 增加 `.app/Contents/Frameworks` 探测；保留每个候选路径的动态加载错误；检查包括 wakeup callback、client API version、render report swap 在内的必需符号；初始化错误通过 `MpvClient::lastCreateError()` 提供。
- **不同 mpv 版本的 loadfile 参数**：参照 wiliwili，client API 2.3 及以上才插入 playlist index；没有 start 时直接发送三参数命令，兼容旧版本。
- **媒体错误被吞掉**：命令返回码与 `MPV_EVENT_END_FILE` 的 error reason 提供 `playbackError(int, QString)` 信号，控制器可立即换备选资源，不必等待超时。诊断只记录命令名和 mpv 错误文本，不打印媒体直链。
- **句柄与渲染上下文释放顺序**：Qt 对象与渲染器共享 mpv handle。客户端析构先卸载 wakeup 回调；渲染器先卸载更新回调、释放 GL render context，最后释放 handle。避免以固定毫秒延迟猜测渲染线程完成时间。独立 notifier 保持回调目标有效，QML item 销毁后 Qt 自动断开其连接。
- **OpenGL 污染和黑底错误**：黑底清理绑定当前视频 FBO，而非默认窗口 FBO；绘制结束恢复 Qt Quick GL 状态。mpv 渲染创建失败通过 `MpvVideoItem::renderError` 传出；每个 handle 的创建失败只报告一次。删除将离屏绘制误当作实际 swap 的时序报告。
- **弹幕时钟**：改为单调时钟与每次媒体进度回报重新锚定，滚动和居中弹幕使用同一个媒体时间；暂停、恢复、倍速不再由各条弹幕自行累计墙钟时间。Seek 对比预测进度，避免高倍速正常轮询被识别为 seek。
- **弹幕布局**：resize、字号、速度、显示区域变化重新建立车道；0% 区域允许零车道；根据字体真实高度计算行高；QPainter 按 ascent 构建基线，修复首行只剩字底部。文字路径按入场缓存、使用粗体一致度量，描边和正文共同应用不透明度。
- **弹幕游标**：只越过连续已结束的前缀，不因后面的弹幕结束而跳过仍在屏的前项；seek 用二分找到可能仍显示的时间窗口。暂停、隐藏和禁用时停止帧定时器。

## 自动验证与边界

`tools/player_runtime_test.cpp` 使用真实动态 libmpv 和项目构建目录中的临时静音 WAV，验证属性格式、pause observation、loadfile start 参数、本地媒体加载错误以及共享所有权。另用内存 QImage 验证弹幕基线、暂停、恢复、尺寸、显示区域及不透明度。构建、执行结果由主任务统一记录。

本次不做 UI 自动化。以下需要用户手测：

1. 普通视频 DASH 与有声 MP4 起播、暂停、继续、拖动、0.5x / 2x / 3x 倍速；确认进度与画面一致。
2. 清晰度和编码切换后仍有声音；一条 CDN 失效时正确尝试下一条；全部失败显示错误，不无限加载。
3. 连续开关播放器、起播时立即关闭、切换视频、最小化恢复与全屏切换，观察崩溃、黑屏及旧画面残留。
4. 缩小再放大窗口，验证弹幕车道和首行中文、英文、emoji；暂停数秒后继续，不突然消失或跳跃。
5. 关闭弹幕、0% 显示区域、低不透明度、顶部和底部弹幕密集场景；Retina/macOS 与 Windows 高 DPI 下观察清晰度。

图形驱动、真实 GPU 解码和跨平台窗口释放仍需在各系统实际播放验证。`audio-add` 沿用 file-loaded 后挂轨的策略，避免将直链塞入逗号分隔的 option 字符串；音轨请求的 HTTP 失败和媒体可用性仍取决于服务端。
