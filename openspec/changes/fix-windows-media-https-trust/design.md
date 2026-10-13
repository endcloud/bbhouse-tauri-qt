## Context

同一 `libmpv-2.dll`（SHA-256 `7469d0493ed908ca4d3ea46681b930ccc29a159f430e4375a0305bba94f3deba`）与公共 HTTPS MP4 对照：curl 开启成功；关闭后 `MPV_ERROR_LOADING_FAILED`，OpenSSL 报证书验证错误；关闭 curl 并传入从 Windows ROOT 导出的 PEM 后成功。curl 使用原生 CA，FFmpeg 的 OpenSSL 路径则需要显式 CA 文件。

## Decisions

1. 保留 `curl-enabled=no` 与空 `http_proxy` 的 libavformat 配置，避免 curl 空代理选项继续读取环境代理。不给失败媒体自动套代理。
2. Windows 使用 `QSslConfiguration::systemCaCertificates()` 读取系统 CA，通过 `QTemporaryFile` 生成 PEM 并传入 `tls-ca-file`；保留 `tls-verify=yes`。不附带易过期的固定 CA 清单，不安装或修改系统证书。
3. CA 文件捕获在共享 mpv handle 的析构器中，渲染器继续持有 handle 时文件仍有效；调用 `mpv_terminate_destroy` 后才删除文件。获取/写入/配置失败时返回明确内核初始化错误，不降低 TLS 验证。
4. 共享本机 HTTPS fixture 同时服务 CTest 与精简包部署门禁。使用仅供测试的证书，不访问凭据、用户历史或远端业务；检查未知证书拒绝、指定测试 CA 后成功、HTTPS 外挂音轨、代理陷阱及 CA 生命周期。

## Risks / Trade-offs

- OpenSSL 使用系统 CA 的快照；运行中修改系统证书后，重建内核才读取新快照。
- 本机 TLS 回归不等于在线业务全部验收；另做公共 HTTPS 与匿名 B 站媒体 GET 对照，签名地址只留进程内存，不发心跳或保存观看记录。

## Validation

构建主程序及 regression-tests，运行定向 HTTPS/音轨/本地原生测试与全量 CTest。提交并确认工作区干净后生成新包，验证包内 HTTPS、真实视频像素、系统媒体控制、PE 依赖、媒体工具与全部 SHA-256。用户验收在线视频、番剧和直播后再归档。
