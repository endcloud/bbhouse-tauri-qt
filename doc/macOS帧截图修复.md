# macOS 帧截图修复

对应 change：`fix-macos-frame-screenshot`。用户于 2026-09-19 确认 macOS 截图修复及单张小于 3 MiB 的压缩测试 pass，已同步主规格并归档至 `openspec/changes/archive/2026-09-19-fix-macos-frame-screenshot/`。

## 原因与修复

macOS 的 `hwdec=auto-safe` 选择直接输出硬件帧的 `videotoolbox`。当前 libmpv 基础渲染模式的帧截图回退到软件转换，本机 mpv 0.41.0 + FFmpeg 9.0.1 实测将 VideoToolbox 格式送入 libswscale，日志为 `Input image format videotoolbox not supported by libswscale` / `Error writing screenshot!`，界面最终显示通用 `error running command`。

仅 macOS 改用 `hwdec=auto-copy`，本机实际选择 `videotoolbox-copy`：保留硬件解码，输出可供截图编码的像素帧。不支持硬解的编码继续由 mpv 软件解码回退。Windows 保留 `auto-safe`。

截图仍调用异步 `screenshot-to-file ... video`，保存到当前应用数据目录的 `Screenshots` 子目录，提示实际保存路径；保留纯视频帧、连续独立文件名和真实失败反馈。截图时不切换解码器、不暂停或重新加载视频。

未选择直接开启 render API advanced control：其更新与线程等待约束需要独立审查，不能直接用于当前 Qt Quick 渲染同步路径。回读模式的代价是每帧增加像素复制和上传，高分辨率播放的内存带宽和功耗仍是此方案的取舍，后续性能回归应持续关注。

## 截图压缩（所有平台）

最终每张图片严格小于 **3 MiB = 3,145,728 字节**。小于上限的 PNG 保留原始字节；达到或超过上限的帧转换为 JPEG，从质量 95 开始逐级降低到 50，优先保留原始分辨率。仍超限时等比缩小后再编码，直到满足大小要求。扩展名与实际编码一致，提示最终 `.png` 或 `.jpg` 路径。

mpv 原始帧先写入每次独立的临时目录，压缩在独立的串行线程池完成，避免阻塞 GUI 或同时重编码多张大图。最终文件通过同目录临时文件发布，禁止覆盖既有图片；超限原始 PNG 与中间文件自动清理。控制器销毁不会让后台任务回调悬空对象。压缩/写入失败显示错误，不误报保存成功。

## 回归覆盖

- 原 `screenshot` 测试仍覆盖无画面失败、无 GPU 的 PNG 视频帧、文件名清理、异步回调及接收对象销毁。
- 新增 macOS 专用 `screenshot-macos`：真实 OpenGL 3.2 Core + libmpv + 合成 H.264 fixture，无可见窗口或 UI 自动操作。
- 同一回归在修复前播放/暂停截图均失败，修复后成功，实际解码器为 `videotoolbox-copy`。
- 验证播放中连续截图、中文含空格路径、解码后尺寸及纯红像素（排除外挂字幕）、暂停时截图、无效目录失败、播放/暂停状态与暂停进度保持。
- fixture 为自生成的两秒红色视频，生成命令记录于测试源文件。运行测试不依赖 FFmpeg 命令行、不访问网络、用户媒体或数据库，临时文件在 build 下自动清理。
- 压缩回归覆盖小 PNG 原字节保留、恰好 3 MiB 的边界、4K 高熵噪声帧压缩、强制等比降采样、损坏/不存在输入、无效目录、PNG/JPEG 不覆盖及临时文件清理。

## 验收记录

实现阶段 macOS Release 与 Qt Creator Debug 构建通过，CTest **32/32**、OpenSpec strict **25/25** 通过。用户于 2026-09-19 确认本次测试 pass；以下保留原手测清单供后续回归。

归档复核：Release 增量构建通过，CTest **32/32** 通过，归档后的主规格 strict **24/24** 通过；无活动 change，工作记录与维护索引已同步。

1. macOS 打开原来报错的视频，播放中和暂停时分别截图，检查 PNG/JPEG 能打开、内容正确、无弹幕或控制面板。
2. 快速连续截图，确认各自保存成功且文件不覆盖；确认暂停帧截图不会恢复播放。
3. 使用日常 4K/高帧率素材，检查起播、拖动、切集与持续播放的流畅度。
4. 截取细节丰富的 4K 视频帧，确认最终图片严格小于 3,145,728 字节，JPEG 画质可接受，保存提示指向实际文件；截图目录不遗留超限 PNG。

Windows 本次未实际运行，初始化选项与纯视频截图命令保持既有行为，新增相同的 3 MiB 压缩流程。离线 H.264 回归不替代在线 HEVC/AV1、HDR 颜色与高分辨率性能手测。
