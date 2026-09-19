# mpv 开发接口

本目录保留 ISC 许可的官方 API 头文件，原版权头不得删除。应用通过 `QLibrary` 加载内核，不链接导入库；旧 `libmpv.dll.a` 已移除。

Windows 开发/本地打包可将可信且架构匹配的 `libmpv-2.dll` 放在此目录（被 Git 忽略）。macOS 开发使用 `brew install mpv`；独立应用则需要正确部署到 `Contents/Frameworks` 并处理传递依赖与加载路径。无需把 DLL/dylib 或完整 mpv 源码提交进本仓库。

二进制的许可证与 API 头文件不同。GPLv3 发行兼容性、对应源码义务、参考下载来源与体积见 [开源说明](../../THIRD_PARTY_NOTICES.md) 和 [发布评估](../../doc/发布准备与mpv分发评估.md)。此处不宣称任何第三方 Windows 构建是 mpv 官方二进制。
