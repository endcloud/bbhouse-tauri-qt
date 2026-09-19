## Decisions

使用现有 Release CMake 构建输出组装标准 Contents/MacOS、Frameworks、Resources、PlugIns。Qt 使用 macdeployqt，libmpv 按 Mach-O 依赖递归打包并重写 install names。禁止依赖开发机绝对路径，系统库除外。可执行文件与所有动态库限定 arm64；最低系统版本不得低于包内任一依赖。

最终输出放项目 build/release 下，重名目录加时间戳，不覆盖旧交付。所有中间文件和测试数据在项目 build 内并清理；不改动外部 Qt/Homebrew 安装。DMG 使用 dmgbuild 写 Finder 布局，不运行 Finder UI 自动化。

## Validation

构建与 CTest；包内 Mach-O 依赖闭包/架构/签名检查；隔离配置、拒绝网络和外部开发库读取的部署冒烟；DMG 校验、只读挂载和副本验证。UI 与实际在线账号播放由用户手测。

## 下载工具部署

三个工具置于 Contents/MacOS，复用 DownloadUtils 的包内优先查找。libmpv 与工具共用按真实来源去重的 Frameworks 依赖闭包；不复制 macOS 系统 curl。记录构建配置、版本和依赖许可；禁止 FFmpeg nonfree 构建。验证原包和 DMG 副本内工具解析/执行与本地媒体处理，另用仅允许本机回环的沙箱验证 aria2/curl 下载合成数据。新包成功后仅清理已识别的旧 macOS 打包目录。

Homebrew aria2 使用 OpenSSL 且默认 CA 路径指向 Homebrew。仅启动 .app 内置 aria2 时设置子进程 SSL_CERT_FILE=/etc/ssl/cert.pem、SSL_CERT_DIR=/etc/ssl/certs，继续验证证书；自定义工具不变。curl 要求 AppleSecTrust。CTest 不全局强制 software 后端，保留原生 OpenGL 测试所需环境。

aria2 还通过运行时加载 OpenSSL legacy provider，静态 Mach-O 列表无法自动发现。包内额外部署 `Contents/Frameworks/ossl-modules/legacy.dylib`，仅内置 aria2 子进程显式设置 `OPENSSL_MODULES`；provider 同样参与依赖闭包、签名与隔离检查。
