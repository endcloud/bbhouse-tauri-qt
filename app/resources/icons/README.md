# 应用图标资源

用户于 2026-09-19 提供 `bbhouse-icon/dist` 中的图标用于本项目全平台导入。首次原样导入四份图像；macOS PNG/ICNS 随后按 Dock 尺寸反馈增加透明留白。构建仅依赖当前目录，不访问原始素材工程。

| 文件 | 用途 |
| --- | --- |
| bbhouse-icon-1024.png | Windows/Linux Qt 窗口、关于页；Linux hicolor 桌面图标 |
| bbhouse-icon-1024-mac.png | macOS Qt 运行时与关于页的透明圆角图标 |
| icon.ico | Windows 主程序及原生服务宿主的多尺寸图标资源 |
| icon.icns | 从带留白 PNG 同步生成的 macOS Finder/Dock 多尺寸图标 |
| source/bbhouse-icon-1024-mac.png | 保留的原始 macOS 素材，仅供重新生成 |
| icon.rc.in | 项目维护的 RC 模板；CMake 写入 ICO 绝对路径，兼容 MSVC/MinGW |

原始 `icon.rc` 仅作为参考，没有沿用其依赖相对工作目录的路径。此处是用户提供的应用品牌素材，不属于第三方组件；本记录不另行推定原始素材的许可。

## macOS 视觉尺寸

Dock 图标主体使用 824×824 像素，居中放在 1024×1024 透明画布上，四边各 100 像素留白。所有 ICNS 尺寸保持相同比例，避免主体铺满画布后比系统应用大一圈。

更新原始素材后，在 macOS 用已安装 Pillow 的 Python 执行 `scripts/generate-macos-icon.py`。脚本始终读取 source 下的原图，同时生成运行时 PNG 和 ICNS；临时 iconset 位于项目 build 并自动清理，不会对已缩小图片反复缩放。正常构建不需要 Pillow。
