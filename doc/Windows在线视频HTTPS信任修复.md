# Windows 在线视频 HTTPS 信任修复

日期：2026-09-26。用户报告 `BBHouse[9c13d4f]` 在线视频提示“媒体装载失败”，本地视频正常，旧包 `BBHouse[e774d74]` 在线播放正常。实现记录：`openspec/changes/fix-windows-media-https-trust`，待用户手测，未归档。

## 根因

新旧包和源码目录的 `libmpv-2.dll` SHA-256 完全一致：`7469d0493ed908ca4d3ea46681b930ccc29a159f430e4375a0305bba94f3deba`。不是更换 DLL 或精简 Qt 对话框资源导致。

近期 `MpvClient::create()` 增加 `curl-enabled=no`，用于解决新版 mpv 的空 `http-proxy` 仍读取环境代理的问题。此举将 HTTPS 取流转为 libavformat；捆绑 FFmpeg 使用 OpenSSL，不会自动读取 Windows 系统 CA。旧 curl 路径则使用原生 CA，因此旧包正常而新包证书校验失败。本地文件及 HTTP 不经过 TLS，故现有 48 项回归和发布门禁当时都通过。

使用同一 DLL 和不含凭据的公共 HTTPS MP4 做对照：

| 配置 | 结果 |
| --- | --- |
| curl 开启，原默认配置 | 成功 |
| curl 关闭，不提供 CA | `MPV_ERROR_LOADING_FAILED (-13)`；OpenSSL `0A000086` 证书校验失败 |
| curl 关闭，显式提供 Windows ROOT CA | 成功，TLS 校验仍开启 |

## 修复

- Windows 内核使用 `QSslConfiguration::systemCaCertificates()` 读取系统可信 CA，生成临时 PEM 并通过 `tls-ca-file` 提供给 libmpv。当前机器得到 79 张 CA；不将固定证书包或用户凭据放入发布目录。
- 保持媒体直连和 `tls-verify=yes`，不修改系统信任、进程代理、解码及渲染参数。CA 生成或配置失败时返回内核初始化错误，不关闭 TLS 校验。
- 临时 CA 文件保持到共享 mpv handle 完全销毁后删除。析构器在 `mpv_terminate_destroy` 后显式释放文件，即便 `weak_ptr` 仍观察控制块也不会延迟清理。
- 本机 HTTPS 回归覆盖默认系统 CA 非空、未知 CA 拒绝、仅测试 handle 信任 fixture CA 后单流与独立音轨加载，以及假代理零连接。测试证书/密钥只用于本机 fixture，不安装到证书库。
- CTest 与精简包共用 `MediaHttpsSmoke.h`；打包脚本强制要求 `DEPLOYMENT_HTTPS_OK`，避免再次只验证 HTTP/本地媒体。打包测试的临时 CA 位于项目 build 中。

## 在线验证边界

使用实际 `MpvClient.cpp` 构建隔离探针，公共 HTTPS MP4 与 **BV1omoHYzEST 的匿名 HTTPS DASH 视频/音轨**均加载成功，TLS 校验为 yes，内核销毁后临时 CA 已清理。B 站只执行公开 GET 解析，不读取 Cookie，不发心跳、不写观看库；签名地址只在 Python 和探针进程内存/匿名管道中传递，不输出、不保存。

该验证覆盖 HTTPS 网络取流与 DASH 音轨装载，不等于登录账号的全部清晰度/番剧权益/直播或真实窗口操作均已验收。用户仍需检查普通在线视频、切清晰度、番剧、直播和本地视频。已有杜比内存台阶问题不属于本修复。

## 自动验证

- Release 主程序与 regression-tests 完整构建成功；CTest **48/48 通过，29.95 秒**，包括 HTTPS 媒体和共享 CA 生命周期新增断言。
- 精简暂存包通过新增 `DEPLOYMENT_HTTPS_OK`、真实视频像素/进度/重开、Windows 系统媒体状态回读及 57 个 x64 PE 依赖闭包；FFmpeg/aria2/curl 检查通过。独立只读复核未发现交付阻塞问题。
- `openspec validate fix-windows-media-https-trust --strict` 通过。此轮为功能回归修复，使用 OpenSpec 记录；后续单纯打包仍遵循免 OpenSpec 约定。

## 修复包交付

- 版本 **BBHouse 2.0.3**；源码提交 `4a3df828d5c99191aaadf867a1c1d1fb801e5c29`。提交后确认工作区干净，再从同源码的已验证 Release/deploy 生成正式包。
- 发布目录：`D:\release\BBHouse[4a3df82]`，启动 `BBHouse.exe`。**192 个文件、310246196 字节（295.87 MiB）**。
- 最终目录再次通过 `DEPLOYMENT_HTTPS_OK`、真实视频/SMTC、57 个 x64 PE 依赖及媒体工具检查；**191 条 SHA-256 全量核验通过**。不含 Cookie、用户数据库或 CA PEM，运行时从系统读取 CA。
- 原发布目录均保留；本轮两个项目内暂存包、临时探针/可执行文件/CA 及日志报告已清理。正常构建输出和既有暂存目录保留，交付文档单独提交，不改变包内源码。未推送 tag 或远程 Release。
- 待用户复测在线视频、番剧、直播及本地视频，change 未归档。
