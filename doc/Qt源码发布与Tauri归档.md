# Qt 源码发布与 Tauri 归档

日期：2026-09-19。OpenSpec：`migrate-public-repository`。

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

**已完成**：远端 `tauri` 仍指向旧 master 的确切 SHA `d22550e971ed5ee7abc30de06ea508f8dde4f6b8`；远端 `master` 已更新到最终发布提交 `fe00f58`（包含 Qt 源码、README 和发布记录）。推送使用绑定旧 SHA 的 `--force-with-lease`，随后通过 `git ls-remote` 核验两个分支。

本次没有创建版本 tag、GitHub Release 或上传二进制包；现有 tag/手动触发 CI 的首次运行仍为原发布准备待办。

原字体再分发授权、默认头像授权、原生图标/安装手测等待办继续见 [发布准备](发布准备与mpv分发评估.md)、[第三方归属](../THIRD_PARTY_NOTICES.md) 和各自活动 OpenSpec change；此次源码迁移不代表这些事项已完成。
