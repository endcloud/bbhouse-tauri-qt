# public 源码同步 2.0.3（2026-09-26）

用户明确要求将主仓库当前 2.0.3 代码同步到 public 并推送。此次仅执行已实现代码的发布同步，不新增功能或修改版本，不建立 OpenSpec change；现有主规格、活动变更和历史归档完整复制，未完成手测的活动变更保留原状态。

## 来源与文件边界

- 主仓库基线：`1496ac2`（macOS 内存优化验证与 2.0.3 Release 打包）；同步前工作区干净。
- public 目录：`/Users/ziyu/Documents/code_g/bbhouse-tauri-qt`，同步前 master 与远端均为 `7c8d66c`；沿用 public Git 历史并普通推送。
- 按主仓库 Git 跟踪普通文件白名单同步源码、内嵌依赖、工具、许可证、doc 与完整 openspec；逐文件 SHA-256 核对。public README 保留用户现有版本且核对字节哈希未变。
- 排除原 `.git`、`b3` 外部链接、忽略及未跟踪文件，包括 `.codegraph`、build、Cookie、真实数据库/历史导出、日志、环境凭据、私钥、用户缓存和 IDE 状态。不读取真实被忽略的用户文件。
- 独立审查发现两个旧 ar 编译库：`3rd/FluentUI/FluentUI/fluentuipluginplugin.lib` 和 `libfluentuipluginplugin.a`，后续 public 白名单排除。前者带本机对象路径；两者不是认证凭据。用户明确同意将这两个库从 public 当前树移除，旧 Git 历史不重写；开发目录原件保留。
- 用户同意清理 public 的 4 个旧 `openspec/changes/add-platform-app-icons/` 文件及空目录。完整归档已同步为 `openspec/changes/archive/2026-09-19-add-platform-app-icons/`，不保留重复活动记录。

## 隐私审查与验证

- 开发跟踪树 **1056 文件，隐私规则零命中**；子代理另行只读检查扩展凭据特征、二进制与证据文件，没有发现真实凭据、签名媒体地址、用户数据库或观看历史。
- `doc/evidence/` 的两份性能 JSON 仅含测试参数、哈希与性能数值；`tools/fixtures/screenshot-red-h264.mp4` 为合成视频素材，均保留。
- 当前主仓库 Qt 6.11.2 Release：主程序和 `regression-tests` 构建通过，CTest **48/48 通过**。发布应用源码与此基线逐文件相同；没有用另一版本的构建结果代替本次验证。
- 开发主规格及活动变更 OpenSpec 全量严格校验 **36/36 通过**。public 提交前校验暂存差异空白、同步清单/内容哈希、完整 OpenSpec，以及跟踪树与可达历史的隐私规则；提交后再次扫描新历史。
- 沿用 `git push origin master`，随后读取远端确认 master 等于本地 HEAD，tauri 仍为 `d22550e971ed5ee7abc30de06ea508f8dde4f6b8`。若 GitHub 规则拒绝，报告实际原因而不擅自改规则或强推。
- 同步暂存树 **1054 文件**、旧 public 可达历史 **833 blob** 隐私规则零命中；**1053 个同步文件**逐一哈希一致，另保留 public README。完整文件集合一致，public OpenSpec **36/36 通过**。
- 完整暂存差异报告 6 行既有 Markdown 双空格换行和 1 处历史规格末尾空行；为保留主仓库文档原文，不做格式改写。忽略行尾空格与文件末空行后的差异检查通过；本轮新增交付文档和维护索引标准空白检查通过。

## 发布边界

本次授权为源码同步和推送，不创建 tag、GitHub Release 或上传二进制包。内存优化中的原生 UI/Windows 手测、杜比主要内存台阶，以及原许可边界继续按既有记录保留。发布验证最终状态以交付回复及远端查询为准，不将文档自身提交号写成永远不变的最终提交号。
