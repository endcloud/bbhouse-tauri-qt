# Qt 源码发布与 Tauri 归档

日期：2026-09-19。OpenSpec：[已验收归档](../openspec/changes/archive/2026-09-19-migrate-public-repository/)。

## 仓库与分支

- 目标仓库：https://github.com/endcloud/bbhouse-tauri-qt 。
- 旧默认 `master`：`d22550e971ed5ee7abc30de06ea508f8dde4f6b8`（`restore normal readme.`），包含 `src-tauri/`。
- 已创建远端 `tauri`，保留上述提交及其完整可达历史。现有 `main`、`dev`、tag 和 Release 不在此次替换范围内。
- Qt 源码基线：原开发仓库 `e4f1144`；本轮另含合成登录测试值缩短及发布记录。
- 发布目录：`/Users/ziyu/Documents/code_g/bbhouse-tauri-qt`；macOS Finder 中文“文稿”对应 `Documents`。
- 发布副本使用独立初始化的 `master` 根提交，不继承开发仓库或 Tauri 的 Git 历史；SSH origin 为 `git@github.com:endcloud/bbhouse-tauri-qt.git`。

## 内容与排除

采用最终 Git 跟踪文件白名单复制并逐文件 SHA-256 核对。完整保留 `app/`、内嵌 `3rd/` 源码、`tools/`、`scripts/`、构建配置、`doc/`、`openspec/`、`.zcode/` 工作流文档、根文档和许可证。

不复制原 `.git/`；不复制 `build/`、IDE 本地状态、Cookie、用户数据库及边车、真实历史导出、特别关注列表、服务配置、日志、缓存、环境凭据、Finder 别名、`.DS_Store` 和临时文件。唯一已跟踪外部参考符号链接 `b3` 也排除，不读取其目标目录。目标重新创建的 `.git/` 仅用于新仓库。

发布前扫描发现 `tools/login_cookie_test.cpp` 的人工编码 Cookie 样例达到现有扫描阈值；已缩短合成值，保持百分号编码、只解码一次等断言，未放宽扫描器规则。旧开发历史仍包含原人工 fixture，不能将旧仓库 `--history` 的该项误报描述为凭据泄漏或零命中；新仓库不继承该历史。

## 验证

- macOS Qt 6.11.2 Release：`bbhouse-qt` 与 `regression-tests` 构建成功；CTest **41/41** 通过，合成值修改后再次确认登录回归。
- 新发布副本 **972 个普通文件**逐一 SHA-256 一致，其中 `doc/` **40** 个、`openspec/` **241** 个文件；排除原跟踪的 `b3` 外部链接。
- 新仓库暂存树与独立历史扫描零命中（首次根提交扫描 823 个去重 blob）。根提交无父节点。
- 新根提交包含第三方及旧规格既有行尾空白；整棵树初次 `diff --cached --check` 因此报告旧问题。保留第三方/历史原文，本次修改文件的空白检查通过。
- 新增 OpenSpec 规格通过严格校验，`git diff --check` 通过。
- 远端 `master` 已使用绑定旧 SHA 的 `force-with-lease` 更新；推送前确认旧提交仍为 d22550e，推送后远端核验通过。

## 发布状态与边界

**已完成**：远端 `tauri` 仍指向旧 master 的确切 SHA `d22550e971ed5ee7abc30de06ea508f8dde4f6b8`；远端 `master` 已切换为独立 Qt 历史，后续 README 普通推送 `5a4bd63` 已核验成功；最新提交以远端实时查询为准。推送使用绑定旧 SHA 的 `--force-with-lease`，随后通过 `git ls-remote` 核验两个分支。

本次没有创建版本 tag、GitHub Release 或上传二进制包；现有 tag/手动触发 CI 的首次运行仍为原发布准备待办。

原字体再分发授权、默认头像授权、原生图标/安装手测等待办继续见 [发布准备](发布准备与mpv分发评估.md)、[第三方归属](../THIRD_PARTY_NOTICES.md) 和各自活动 OpenSpec change；此次源码迁移不代表这些事项已完成。

## 后续 public 发布流程（项目记忆）

1. 明确两个独立仓库：开发目录 `/Users/ziyu/Documents/code_trae/bbhouse-qt`；public 发布目录 `/Users/ziyu/Documents/code_g/bbhouse-tauri-qt`，origin 为 `git@github.com:endcloud/bbhouse-tauri-qt.git`。先检查两个工作区及远端 SHA；发布目录自己的 README 等用户修改必须保留并审阅。
2. 同步已确认的发布范围：以 Git 跟踪普通文件为白名单，保留全部文档、OpenSpec、许可证和内嵌依赖。禁止整目录复制或同步原 `.git`、外部 `b3` 链接、Cookie、真实历史/用户数据库、环境凭据、日志缓存、构建产物；复制后核对清单与内容哈希。开发端的新版本不因文档维护自动发布。
3. 首次迁移已完成。后续沿用 public 仓库现有 Git 历史，**不重新 init，不改写 tauri，不默认强制推送**。差异审阅后提交，正常 `git push origin master`；若远端领先，先获取并处理差异，不覆盖他人更新。
4. 提交前按项目要求构建主程序及 regression-tests、执行 CTest。文档变更若复用已有构建证据，应明确来源和范围；不能把不同源码版本的构建当作本次发布版本验证。执行 `git diff --check` 和 OpenSpec 严格校验，在 public 仓库运行 `python3 tools/privacy_audit.py --history`。开发旧历史中的已知合成 fixture 命中应单独记录，不放宽扫描规则。
5. 检查推送结果后用 `git ls-remote origin refs/heads/master refs/heads/tauri` 核对：master 等于本地 HEAD，tauri 保持 `d22550e971ed5ee7abc30de06ea508f8dde4f6b8`。工作区应干净，交付与任务清单记录实际结果；记录已发生的提交，不追逐“文档自身最终提交号”。
6. 普通源码推送不等于创建 tag 或二进制 Release。tag/手动构建、产物分发和既有许可/原生手测边界按各自任务处理。一次发布授权不代表今后自动发布所有开发改动。

### GitHub 认证与规则

- 当前 Git 传输通过 macOS 本机 SSH 公钥认证为 `endcloud`；不是浏览器或 Codex 的 GitHub 登录会话。Git 的 `user.name`/`user.email` 只是提交署名，不是认证凭据。不要将私钥、令牌或其他认证材料复制到项目、日志或提交中。
- macOS 的 `osxkeychain` credential helper 用于 HTTPS 凭据；本次 SSH 推送不走该认证路径。迁移时 HTTPS 曾因缺少可用凭据失败，SSH 成功。
- 经典 Branch protection 和 Repository Rulesets 是独立检查；历史迁移先后遇到 GH006 和 GH013 的 force-push 拒绝。不得为通过推送擅自关闭规则；维护者已处理此次迁移的临时设置。
- README 提交 `5a4bd63` 的普通推送成功，但 GitHub 同时报告 `Bypassed rule violations`：提交没有 verified signature，且没有通过 PR。说明当前推送身份具有适用的 bypass 权限，**不是这两项规则已被满足**；权限配置可能改变，不能保证未来仍可直推。
- SSH 登录与 Git 提交签名是两回事：SSH 传输认证成功不会自动给 commit 签名。若将来规则要求而没有 bypass，应配置被 GitHub 认可的签名或使用 PR，并按实际错误处理；不自动扩大权限或改写历史。

## 用户验收与同步归档

用户确认结束并要求同步归档。此次只归档 `migrate-public-repository`，同步 `repository-publication` 主规格；其他发布准备、平台打包或手测任务维持各自状态。发布记忆同时写入两个仓库的 AGENTS.md 与本文，public README 保留用户版本。

归档验证：两个仓库 OpenSpec 全量严格校验均通过。开发库当前 2.0.2 的构建与 CTest 41/41 通过，仅证明开发树；public 本轮只改文档/规格，应用源码沿用上一轮 1.0.1 的构建与 CTest 41/41 证据，未混同版本。public 新历史另行隐私扫描；本轮不创建临时文件。
