## Context

起始源码提交为 e4f1144；当前项目工作区干净。目标远端默认 master 为 d22550e971ed5ee7abc30de06ea508f8dde4f6b8，含 src-tauri；tauri 尚不存在，master 显示 protected。Finder 的“文稿”对应 /Users/ziyu/Documents。

## Decisions

- 原项目的 Git 历史不复制到新仓库。以跟踪普通文件为白名单，逐文件复制并比较 SHA-256；不跟随外部符号链接。
- 完整保留 doc、openspec、根文档和 .zcode 中的工作流文档。只排除本机外部参考链接 b3；.gitignore 已覆盖 Cookie、用户数据库、日志、缓存、构建目录等。
- tauri 必须指向观察到的旧 master；创建时使用空值 lease，禁止覆盖已被其他人创建的同名分支。核实归档后，master 使用旧 SHA 的 lease 防止覆盖并发更新。
- 远端保护若拒绝更新，不擅自关闭规则；保留新仓库与明确的后续步骤。
- HTTPS 没有可用认证，改用已认证为 endcloud 的 SSH；不读取或输出认证材料。
- 隐私扫描保持原规则；仅缩短人工 Cookie 编码样例。旧开发仓库历史仍包含该人工 fixture，新发布仓库不继承此历史。

## Validation

当前项目构建 bbhouse-qt 和 regression-tests，运行 CTest；新仓库从清单核对完整性、根提交父节点、敏感路径及可达历史。所有新规格通过 OpenSpec 严格校验；推送后分别读回 tauri/master 并核对 SHA。此次源码迁移不创建版本 tag 或二进制 Release。
