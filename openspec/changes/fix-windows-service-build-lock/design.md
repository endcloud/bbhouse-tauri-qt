## Context

项目用同目录的原生宿主和 Qt 应用 worker 实现 Windows 常驻服务。服务从 Debug 目录注册后，即使关闭 GUI，Session 0 的两个进程仍持有可执行文件，Windows 链接器无法覆盖正在运行的 EXE。

## Goals / Non-Goals

- 恢复原 Qt Creator 构建目录的可编译状态，保留现有服务和用户数据。
- 不把 Windows 文件锁误判为源码或 MinGW 链接配置错误。
- 不自动杀进程、卸载服务、提升构建权限或修改 ACL。

## Decisions

1. 先用 verbose 构建找到链接器的首个具体错误，再通过 SCM 的宿主路径确认与失败输出一致。
2. 用户在现有服务管理中暂停并确认 UAC，随后退出 GUI；正常停止会通知 worker 收尾，等待宿主和 worker 退出后再链接。
3. 使用原 MinGW Makefiles / Qt 6.11.1 Debug 目录复验，测试仅使用离线 fixture 和项目 build 内临时数据。
4. 将恢复说明放到 README 的 Windows 构建段，并在服务交付文档记录证据。不为文档维护虚构规格变化。
5. `configure_file(COPYONLY)` 将中文名 Markdown 暂存为构建目录中的 `help/cookie-import-help.md`，让 make/RCC 的物理文件依赖仅含 ASCII；`QT_RESOURCE_ALIAS` 继续提供 `:/help/Cookie导入帮助.md`。CMake 跟踪原文档的更新，不额外维护一份源文档。

## Risks / Trade-offs

- 服务暂停期间不会执行定时同步；需要时由用户在构建后重新启用。长期运行应注册独立部署目录中的服务，避免开发编译覆盖运行文件。
- 若仍报 Permission denied，需要继续检查残留 GUI、worker 或其他占用；仅凭最后一行 Error 1 无法确认原因。
- 全部编译需要足够磁盘空间；本机 C 盘仅剩约 10 MB 时已实际出现 `No space left on device`。项目外清理需由用户处理或明确授权。
