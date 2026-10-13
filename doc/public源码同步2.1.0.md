# public 源码同步 2.1.0（2026-10-13）

用户要求将私有主仓库当前 2.1.0 同步到 public 并推送。本次按发布约定执行源码同步，不新增或归档 OpenSpec change；完整保留主规格、活动变更和历史归档，尚未完成的 UI/Windows 手测继续保留原状态。

## 来源与同步边界

- 应用源码基线：`788cb7dd71335163d351dd95c0413af4c5c8db32`。CMake 版本 **2.1.0**，version-code **11**；私有仓库会话开始时工作区干净。
- public 目录：`/Users/ziyu/Documents/code_g/bbhouse-tauri-qt`，原 master 与远端均为 `4e88e67f6d6ffe2d9f4fbe11cef7bf878de39e2d`。沿用 public 独立历史，以普通推送更新 master。
- public 会话开始前存在 README 修改，包含会员/充电权限提示、个人偏好、开发指引、链接与致谢。已向用户汇报，用户明确要求“拷贝完一起提交”；原文保留并与发布更新一起提交，不以开发 README 覆盖。
- 同步 Git 跟踪普通文件，保留源码、内嵌依赖、许可证、工具、doc 与 openspec；逐文件核对 SHA-256。排除私有 `.git`、`b3` 外部链接、两份旧编译库 `fluentuipluginplugin.lib` / `libfluentuipluginplugin.a`，以及忽略/未跟踪的 `.codegraph`、`.tmp`、build、IDE 状态、Cookie、用户数据库/观看记录、环境凭据和日志缓存。
- public 没有需要删除的独有跟踪文件。本轮不删除项目外文件，不修改 tauri 分支或改写已有历史。

## 隐私审核

原隐私扫描在 1072 个跟踪文件中仅命中 `app/core/MediaHttpsSmoke.h` 的固定测试私钥。独立审查确认：它与合成 localhost 服务证书匹配，只供回环 HTTPS 回归使用；测试服务与代理仅监听 LocalHost，CA 仅提供给本次测试 handle，不安装系统信任库，也不用于任何账号或生产服务器。

为保留 Windows HTTPS 拒绝/信任/独立音轨门禁并让发布审计正确识别公开测试材料，隐私工具仅按**精确路径与完整 PEM 的 SHA-256**认可这一固定 fixture。不忽略整个文件，不豁免其他路径、未知或修改后的私钥，也不影响同文件内其他凭据规则。测试密钥正文不写入交付报告或工具输出。

扩展凭据、签名 URL、数据库与异常二进制复核未发现真实隐私材料。真实 ignored Cookie/数据库/私钥未被打开或复制。已知公开测试 fixture 保留不等于允许发布真实私钥。

## 验证与发布

- 当前应用基线的 macOS Qt 6.11.2 Release 主程序与全部 `regression-tests` 构建通过，CTest **48/48 通过**。public 应用源码逐文件核对一致；Windows 原生 HTTPS 的既有验证及手测边界见 [HTTPS 修复记录](Windows在线视频HTTPS信任修复.md)，不把本次 macOS 结果视为 Windows 原生复验。
- 主仓库 OpenSpec 全量严格校验 **38/38 通过**；public 同步后再次校验完整规格和文件清单。发布阶段不重新初始化 Git、不创建 tag 或二进制 Release。
- 隐私工具 **7 项回归全部通过**，覆盖固定 fixture、改动/新增/其他路径私钥、残缺头部与尾部，以及同文件内额外 GitHub 凭据。修补前固定 fixture 用例按预期失败、其余 6 项通过；修补后主仓库隐私扫描零命中。暂存树及新 public 可达历史另行复扫后推送。
- 推送后读取远端确认 master 等于本地 HEAD，tauri 继续保持 `d22550e971ed5ee7abc30de06ea508f8dde4f6b8`；实际推送结果及提交号在交付回复中记录。
- public 暂存树 **1071 个文件**，其中 **1070 个同步文件**逐一哈希一致，另保留用户 README；暂存树与此前 public 可达历史 **1018 blob** 隐私规则零命中，固定测试密钥按上述精确规则识别。public OpenSpec **38/38 通过**。
- 全部同步代码和新增文档的标准空白检查通过；用户 README 有 5 行既有/本次编辑的行尾空格，包含 Markdown 硬换行。按保留原文约定不改写 README，其余差异检查正常。项目内 `.tmp` 回归临时内容已清理。

既有许可、杜比内存台阶、功能手测待办不因源码同步自动完成。提交署名包含 Codex；本次用户 README 与版本同步按用户明确要求合并为一个 public 提交。
