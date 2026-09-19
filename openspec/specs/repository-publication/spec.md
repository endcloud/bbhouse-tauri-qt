# repository-publication Specification

## Purpose
TBD - created by archiving change migrate-public-repository. Update Purpose after archive.

## Requirements

### Requirement: 保留原 Tauri 历史

发布流程 SHALL 在将目标仓库 master 切换为 Qt 源码之前，将旧 master 的确切提交及可达历史保存到 tauri 分支。

#### Scenario: 更新默认分支

- **WHEN** 发布 Qt 版本到现有 Tauri 仓库
- **THEN** tauri 指向更新前核实的 master 提交；master 更新使用对应旧 SHA 的 lease，遇到并发更新或保护拒绝时停止并报告

### Requirement: 独立且不含用户数据的源码副本

发布仓库 SHALL 新建 Git 历史，完整保留源码、doc、openspec 和许可文件，排除原 .git、用户凭据、观看记录、数据库、构建产物及指向项目外的本机参考链接。

#### Scenario: 核对发布副本

- **WHEN** 从开发仓库准备发布根提交
- **THEN** 按审计后的清单复制并核对文件内容，扫描跟踪树与新可达历史，确认不含敏感路径和规则命中的凭据

### Requirement: 可复核的发布记录

发布流程 SHALL 记录来源提交、旧 Tauri 提交、目标目录、排除项、验证结果以及远端实际状态，并保留未完成的发布和手测边界。

#### Scenario: 完成交付

- **WHEN** 发布操作完成或被外部条件阻塞
- **THEN** 交付文档与任务清单反映实际结果，不将未通过的远端更新、平台手测或许可清理描述为完成
