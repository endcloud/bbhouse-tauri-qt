## Why

用户报告 Windows 新包在线视频提示“媒体装载失败”，本地视频正常，旧包 `BBHouse[e774d74]` 正常。新旧 libmpv 完全相同；近期为隔离环境代理禁用 curl 后，FFmpeg/OpenSSL 不会自动使用 Windows 根证书库，HTTPS 证书校验失败。现有 HTTP 和本地媒体门禁未覆盖此路径。

## What Changes

- Windows 媒体内核为 FFmpeg 显式提供 Qt 读取的系统 CA，保留媒体直连和 TLS 校验。
- CA 临时文件与共享 mpv handle 同寿命，内核销毁后自动清理，不写入账号或历史数据。
- 增加本机 HTTPS 单流、独立音轨、证书拒绝和代理隔离回归，并加入精简发布包门禁。
- 完成构建、CTest 和隔离验证后重新打包至 `D:\release`，保留已有包。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `regional-bangumi-proxy`：媒体直连同时保持 Windows HTTPS 系统证书信任。

## Impact

影响 MpvClient Windows 初始化、原生媒体回归和发布门禁。不修改解码或渲染参数，不关闭证书校验，不修改系统证书、代理、服务、ACL、Cookie 或用户数据库。
