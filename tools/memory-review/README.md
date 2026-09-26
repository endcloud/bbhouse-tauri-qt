# 播放内存诊断工具

2026-09-21 审查使用。只读取指定 libmpv 的选项或播放本地合成片源，不修改 BBHouse 设置，不读取 Cookie、历史库、网络地址或用户配置。原生探针使用不可见 `QOffscreenSurface`、Qt OpenGL 和 libmpv render API；没有 QML 页面、弹幕或在线缓存。所有输出是白名单选项与内存计数，不打印播放路径/媒体直链，也不打开 mpv 原始日志。

后续修复新增显式真实片源获取工具 `fetch_bilibili_fixture.py`；它独立于上述离线探针，仅在用户指定 BV 和 `--cookie-file` 时读取既有凭据，执行 nav/view/playurl 和 CDN 的 GET。凭据和签名地址只在进程内存中存在，不进入日志、命令行子进程或磁盘。不调用心跳/历史接口；下载的是指定视频流前缀，不是完整视频。

```powershell
python tools/memory-review/fetch_bilibili_fixture.py --bvid BV1omoHYzEST --cookie-file bilibili.cookie.txt --output-dir build/memory-fix-media --download --sample-mib 64
```

默认取得 qn80 与 qn126；`--quality` 可重复指定。若本机账号无该档位，输出 streams 中会缺少对应项，不能把其他档位当作杜比。已有同名 fixture 会拒绝覆盖。媒体前缀可能不足以播放至原视频结尾，测试必须限制在实际可读区间内；文件位于 build 下，结束后删除。只提交白名单测量数据，不提交媒体、Cookie 或响应原文。

`mpv-memory-probe --hw-threads 1/2/4` 可比较 **hwdec-threads**，它不同于 `--threads` 对应的 **vd-lavc-threads**。当前 DLL 默认前者为 4。实验参数不自动写入产品默认值。

`probe_native_d3d11.py` 是第三种对照：隐藏原生窗口 + mpv `vo=gpu` 的 D3D11 呈现，用同一 DLL/本地前缀验证直接硬解是否实际工作。它不是 Qt render API，也没有 Qt Quick 弹幕/字幕/控件合成，不能直接替换产品播放器。命令如下，输出仍为固定属性和内存样本：

```powershell
python tools/memory-review/probe_native_d3d11.py --dll 3rd/mpv/libmpv-2.dll --fixture build/memory-fix-media/BV1omoHYzEST-qn126.m4s --seconds 20 > build/memory-fix-media/dolby-native-d3d11.json
```

产品运行时诊断可用 `$env:BBHOUSE_PLAYER_DIAGNOSTICS='1'` 显式开启，随后从控制台启动 BBHouse，每 5 秒输出 `BBHOUSE_PLAYER_DIAGNOSTICS` 开头的紧凑 JSON。默认关闭；结束后 `Remove-Item Env:BBHOUSE_PLAYER_DIAGNOSTICS`。只含实际解码/格式/缓存/掉帧等白名单和 Windows 进程私有提交量、总工作集，不含路径、标题、URL 或 Cookie。字段不可用时数值字段省略，不把缺失值伪装为零。`demuxer-cache-state` 只读取其数值子字段，不导出含端点的整个对象。

## 两种探针

- `probe_mpv_memory.py`：Python 标准库 + ctypes。无片源时读取本机 DLL 默认值；带片源时使用 `vo=null`、`hwdec=no` 测量纯软件解码，**不能代表实际 OpenGL 播放器**。
- `mpv_memory_probe.cpp`：独立 Qt/CMake 小程序。使用与播放器同样的 `vo=libmpv` / OpenGL render API / 单 FBO，默认 `hwdec=auto-safe`，记录实际 `hwdec-current`、像素格式、私有提交量与工作集。默认输出 FBO 为 1920×1080，允许改变输出尺寸。

Windows 专用。依赖已有 Python 3、FFmpeg/libx265、Qt 6 MinGW、CMake、Ninja；不会自行安装软件。请在仓库根目录用 PowerShell 7 执行。

## 准备与构建

```powershell
New-Item -ItemType Directory -Force build/memory-review | Out-Null
$env:PATH = 'C:/Qt/Tools/mingw1310_64/bin;C:/Qt/6.11.1/mingw_64/bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' -S tools/memory-review -B build/memory-review/probe -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe -DCMAKE_BUILD_TYPE=Release
& 'C:/Qt/Tools/CMake_64/bin/cmake.exe' --build build/memory-review/probe -j 2
```

更换机器时调整 Qt/CMake/编译器路径。必须使用 `windows` QPA 配合不可见离屏 surface，避免把软件 `offscreen` 平台误当成真实 Intel OpenGL 驱动。

只生成测试图，不打开真实媒体。两个文件均为 4 秒、24 fps、HEVC Main 10、YUV420P10，无音频、无 Dolby Vision/HDR 元数据。以下命令会覆盖同名合成片源，目录仅用于此探针。

```powershell
ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=3840x2160:rate=24:duration=4' -pix_fmt yuv420p10le -c:v libx265 -preset ultrafast -x265-params 'pools=4:frame-threads=2:log-level=error' -crf 28 -an -y build/memory-review/synthetic-4k-main10.mp4
ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=1920x1080:rate=24:duration=4' -pix_fmt yuv420p10le -c:v libx265 -preset ultrafast -x265-params 'pools=4:frame-threads=2:log-level=error' -crf 28 -an -y build/memory-review/synthetic-1080p-main10.mp4
```

## 复现命令

读取 DLL 默认选项；本次 `3rd/mpv` 和 `build/bin` 的 DLL SHA-256 一致，详见证据 JSON。

```powershell
python tools/memory-review/probe_mpv_memory.py --dll 3rd/mpv/libmpv-2.dll > build/memory-review/default-options.json
$probe = 'build/memory-review/probe/mpv-memory-probe.exe'
$probeArgs = @('--dll', '3rd/mpv/libmpv-2.dll', '--fixture', 'build/memory-review/synthetic-4k-main10.mp4')
& $probe @probeArgs > build/memory-review/4k-gl-auto.json
& $probe --dll 3rd/mpv/libmpv-2.dll --fixture build/memory-review/synthetic-1080p-main10.mp4 --next-fixture build/memory-review/synthetic-4k-main10.mp4 > build/memory-review/switch-gl-auto.json
```

`--next-fixture` 按“首片源 → 下一片源 → 首片源”在同一个 mpv handle 和 render context 中依次播放，每阶段采样 6 秒。每次切换使用 `loadfile replace`，与产品切清晰度方式一致。四秒片源结束后按 `keep-open=always` 保留末帧，统计包含起播、播放与末帧保留期。

本次已做的诊断 A/B 可按以下方式复现。它们是隔离实验，**不是推荐的产品默认值**。

```powershell
& $probe @probeArgs --extra 4 > build/memory-review/4k-gl-extra4.json
& $probe @probeArgs --extra 0 > build/memory-review/4k-gl-extra0.json
& $probe @probeArgs --threads 4 > build/memory-review/4k-gl-threads4.json
& $probe @probeArgs --hwdec d3d11va > build/memory-review/4k-gl-d3d11va.json
& $probe @probeArgs --hwdec dxva2-copy > build/memory-review/4k-gl-dxva2copy.json
& $probe @probeArgs --hwdec no --threads 4 > build/memory-review/4k-gl-software4.json
& $probe @probeArgs --direct-render no > build/memory-review/4k-gl-no-dr.json
& $probe @probeArgs --fbo-format rgba8 > build/memory-review/4k-gl-rgba8.json
& $probe @probeArgs --scale bilinear --dscale bilinear > build/memory-review/4k-gl-bilinear.json
python tools/memory-review/probe_mpv_memory.py --dll 3rd/mpv/libmpv-2.dll --fixture build/memory-review/synthetic-4k-main10.mp4 --threads 0 > build/memory-review/4k-threads-auto.json
python tools/memory-review/probe_mpv_memory.py --dll 3rd/mpv/libmpv-2.dll --fixture build/memory-review/synthetic-4k-main10.mp4 --threads 4 > build/memory-review/4k-threads-4.json
```

工具默认 `scale=lanczos`、`dscale=hermite`、`fbo-format=auto`、`vd-lavc-dr=auto`，均与本次实际 DLL 无配置初始化后的默认值一致。将这些值显式写入工具，是为了复现本次参数，不代表其他 mpv 版本默认值相同。所有 A/B 都关闭压缩数据缓存，并使用空音频输出。

## 读取结果与边界

`private_mib` 是 Windows `PROCESS_MEMORY_COUNTERS_EX.PrivateUsage`（私有提交量）；`working_set_mib` 是总工作集，包含共享页，**不等同于任务管理器进程页常见的活动私有工作集**。单位为 MiB；没有读取 GPU 专用/共享内存计数，不能把这两个数字相加或直接称作显存。

原生输出 `after_stop_render_context_alive` 仍保留 mpv render context；`after_mpv_destroy_gl_alive` 已释放 mpv handle 和 render context，但 Qt OpenGL context/FBO、驱动分配器与进程仍存活。两者都不是进程和图形资源完全退出后的数值。

本次 Intel Arc 140T / 驱动 32.0.101.8861 / mpv v0.41.0-1049-g0b7ed670f 下，默认路径实际为 `d3d11va-copy`，不是零拷贝。原生三阶段末值私有提交量约 537 → 1350 → 669 MiB，已在没有 QML/弹幕/CDN 缓存的条件下重现分辨率相关的大幅增长和回落。明确指定 `d3d11va` 后实际回落软件解码，不应直接作为修复。

采样只是一个四秒合成 Main 10 片源、固定 1080p 输出 FBO；未复现 Dolby Vision 元数据、HDR 色彩处理、在线 DASH 独立音轨、真实码率、Qt Quick 合成或 PotPlayer。少量丢帧包含首次 shader 编译/起播以及探针调度；此工具不是 QoE/帧率基准。单次峰值受驱动分配缓存和运行顺序影响，不能将所有差值归为某一个内部缓冲池。

`hwdec-extra-frames=0/4`、线程数 4、关闭 direct rendering 在本机硬解复制路径上均未明显降低峰值。RGBA8 和 bilinear 约降低百余 MiB，但会改变中间精度/缩放质量，尤其不能据此直接降低 HDR/DV 处理质量。软件线程 0→4 的明显差异只在软件解码路径成立。

已保存的精简结果在 `doc/evidence/memory-review-2026-09-21.json`。中间可执行文件、合成片源和完整样本均位于 `build/memory-review`，不应提交。无用户数据或服务操作。
