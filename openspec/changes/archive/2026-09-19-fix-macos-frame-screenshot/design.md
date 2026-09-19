## Context
当前 `vo=libmpv` 未启用 render API advanced control，mpv 截图回退为当前解码帧的软件转换。macOS `hwdec=auto-safe` 选择 `videotoolbox`，实测截图日志出现 `Input image format videotoolbox not supported by libswscale`。对照 `auto-copy` 选择 `videotoolbox-copy` 后，相同素材成功落盘。

## Decisions
- 仅 macOS 初始化使用 `hwdec=auto-copy`，由 mpv 选择带回读的硬解器并保留不可用时的软件解码回退。Windows 继续 `auto-safe`。
- 不在点击截图时切换解码器，避免 seek、暂停或重启播放；截图继续通过异步 `screenshot-to-file ... video` 完成。
- 不启用 `MPV_RENDER_PARAM_ADVANCED_CONTROL`：它要求更新通知始终驱动独立渲染线程，当前 Qt Quick 隐藏/同步阶段不能保证该约束，直接启用存在死锁风险。
- 回读模式保留硬件解码，但增加像素帧复制开销；高分辨率实际播放流畅度列入手测。
- macOS 专用回归使用 QOffscreenSurface + OpenGL 3.2 Core 与真实 libmpv，无可见窗口或 UI 自动操作；合成 H.264 fixture 固定存入仓库，运行时不依赖 FFmpeg 命令行。

## Validation
先以旧选项验证回归失败，再验证修复后通过；检查连续保存、PNG 像素、字幕隔离、播放/暂停状态与写入失败。构建 Release/Debug、运行全部 CTest 和 OpenSpec strict 校验。真实在线高分辨率播放由用户手测。

## Screenshot Size Limit
- 用户确认 macOS 截图恢复，追加要求单张小于 3 MiB（3 × 1024 × 1024 = 3,145,728 字节）。此规则覆盖 macOS/Windows。
- mpv 先写入截图目录下每次独立的临时目录；后台处理完成并验证大小后才发布最终图片，超限原始帧不作为最终截图保留。
- 严格小于上限的 PNG 原样保存；达到/超过上限时转为 JPEG，优先保持分辨率逐级下调质量，仍不满足时等比缩小并重新编码。
- 重编码在单独串行线程池执行，避免 GUI 阻塞和连续截图时并发大图压缩引起内存峰值；完成通过 Qt future 通知 GUI，对象销毁后不回调悬空对象。
- 最终扩展名与图片编码一致，不覆盖已有截图，保存成功提示真实路径；失败提示错误并清理临时产物。截图不改变播放状态。
