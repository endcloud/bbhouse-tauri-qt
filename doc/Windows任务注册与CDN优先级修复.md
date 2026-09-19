# Windows 任务注册与 CDN 优先级修复

> 后续变更：Windows 任务计划部分已按用户要求取消，改用 [Windows Service + UAC](Windows原生服务与UAC.md)。本文 Windows XML 分析为历史记录，不再作为当前方案；媒体 CDN 改动保留。

日期：2026-09-19。OpenSpec：`fix-scheduler-registration-and-cdn-priority`。用户于 2026-09-19 确认 apply 完成且 Windows 手测 pass，当前媒体 CDN 范围已同步主规格并[归档](../openspec/changes/archive/2026-09-19-fix-scheduler-registration-and-cdn-priority/)。

## Windows 注册根因

原 `HistoryScheduler::applyWindowsXml()` 写入 `QTemporaryFile`，调用 `close()` 后立即执行 `schtasks /Create /XML`。Qt 的临时文件为支持 reopen 会保留底层文件句柄；`close()` 不等同于释放 Windows 原生句柄。外部命令重新打开时可产生共享冲突，与用户的“另一个程序正在使用此文件，进程无法访问”一致。

依据：[Qt 6.11.2 qtemporaryfile.cpp](https://github.com/qt/qtbase/blob/v6.11.2/src/corelib/io/qtemporaryfile.cpp)，`QTemporaryFileEngine::close()` 保留句柄直到移除/析构。回归用 macOS 原生描述符检测验证：Git 原始实现失败于句柄释放断言，修复实现通过。Windows 分支使用 `CreateFileW` 独占打开验证相同约束，本机没有 Windows 执行环境。

修复改为唯一 `QTemporaryDir` 下的普通 `QFile`：完整写入、flush、析构关闭文件，再让 `schtasks` 读取。注册、修改和回滚共用该路径。无论成功、失败、超时或命令无法启动，临时文件及目录均由 RAII 清理。

此错误与 Debug 编译、UAC 权限不是同一问题。任务继续使用当前用户 `InteractiveToken` / `LeastPrivilege`；不新增管理员 manifest 或自动提权。真正的权限/企业策略拒绝仍沿用错误与回滚处理。

## 播放加载链路检查

- 普通视频通过 WBI `/x/player/wbi/playurl`，DASH 使用 `fnval=4048`；PGC 使用 `/pgc/player/web/playurl`，两者共用 DASH 与 durl 解析。
- DASH 仍按既有清晰度、编码和音频规格选流，随后对所选段的主备地址排序。DASH 解析/媒体候选耗尽时进入原有 durl 回落；本次不引入额外探测请求，不改写 CDN 域名、签名或权益参数。
- 区域番剧继续仅对 API 使用请求级代理快照。普通 API 和 libmpv 媒体直连，不改变全局代理。

原实现仅识别主机含 `mcdn` 的 DASH 地址；音频虽解析备选但只传首地址；durl 只保留一个地址；直播只在每条协议/编码轨内部降级 mcdn，导致前一轨的 P2P 仍可能先于后一轨的普通 CDN。

现在 `MediaUrlPolicy` 统一处理全部播放路径：

| 优先级 | 节点 | 行为 |
| --- | --- | --- |
| 1 | 已知云厂商 UPOS 节点（cos、hw、oss、ali、kodo、ks3、bos、zos、akam），限定 bilivideo.com / bilivideo.cn / akamaized.net 域边界 | 首选 |
| 2 | 其他有效普通 HTTP(S) 节点 | 保留接口原序 |
| 3 | 主机含 mcdn/pcdn、szbdyd.com 及其子域 | 仅备选，仍可播放 |

只按主机分类；不把查询参数中的 `mcdn` 或非标准端口判为 P2P。过滤无效地址并按完整 URL 去重，保留原始 URL 文本。未知新域名暂归普通节点；域名规则无法保证识别未来所有 PCDN 部署，也不能保证服务端一定下发云 CDN 地址。仅有 P2P 时保留所有有效备选。

- DASH 的视频及音频独立应用同一排序；同时读取 camel/snake 备选数组，空字段不能遮蔽另一个有效字段。
- 外挂音轨在视频主文件装载后异步逐条尝试，单条失败或超过 8 秒则取消并尝试下一条；音轨耗尽才交还原有视频/单流回退。主视频的 8 秒看门狗在音轨阶段停止，避免抢先中断音轨回退。成功停止后续候选；切播/停止会清空候选并隔离旧命令回复。
- durl 保留第一分段的全部主备链，失败/超时继续下一条，耗尽报错；未扩展原有多分段拼接能力。
- 直播在实际清晰度一致的所有候选合并后统一排序。同级仍沿用 AVC FLV、AVC HLS、HEVC 等既有偏好；普通 HLS 可先于 P2P FLV，不混入其他清晰度。

## 自动验证

- macOS Qt 6.11.2 Release 主程序与全部回归目标、Qt Creator Debug 主程序构建通过。
- 完整 CTest：32/32 通过（包含本机截图环境测试）。
- `history-scheduler`：55 项断言，覆盖原生句柄释放、XML 完整、最低权限及失败/超时/启动失败/回滚/临时文件清理；使用调度命令替身，未注册真实系统任务。
- `api-regression` / `live-api`：离线响应覆盖云/普通/P2P 排序、视频和音频主备链、camel/snake 字段、签名文本保持、去重、纯 P2P、无效地址、durl 与直播跨轨排序。
- `player-runtime`：真实 libmpv、本地 HTTP 媒体验证音频 404→成功、按序且成功即停、全部失败、8 秒停滞→成功、旧超时回复隔离、切播/停止不访问遗留备选。没有访问真实媒体、账号或观看记录。
- OpenSpec strict 校验、`git diff --check` 通过。临时测试文件位于项目 build 并清理。

以上为当时自动验证记录，不单独代表线上 CDN 或 GPU 音画验收；当前媒体 CDN 变更的 Windows 手测通过结论来自用户于 2026-09-19 的明确反馈。旧 XML 注册方案已取消，不属于本次归档验收范围。

## 原 Windows Debug 手测（历史方案，已废弃）

1. 使用普通权限 Qt Creator 重新构建并运行最新 Debug。打开“本地历史 → 服务管理”并确认注册，应无文件占用报错，也不要求 UAC。
2. 在任务计划程序中确认任务属于当前用户，未勾选“使用最高权限运行”；操作指向当前 Debug/部署程序和绝对路径配置。
3. 修改日/周计划并保存，刷新状态，暂停/启用后确认与系统一致。原有数据和配置保持。
4. 设置几分钟后的计划，保持登录、关闭主窗口，触发后检查“最近运行”的 scheduled 结果。
5. 若注册成功但任务独立启动失败，检查 Debug 程序的 Qt/编译器 DLL 是否可从任务环境找到：任务不会继承 Qt Creator 注入的 PATH。正式定时运行推荐使用完整 Release 部署目录，重新保存计划绑定该程序；这与此次 XML 文件占用是两个独立问题。

## 播放手测与后续回归

1. 普通视频、番剧（含区域番剧）各播放一条，确认视频和音频正常，切清晰度/编码后仍有声音并保位。
2. 测试首选 CDN 不可用的场景，确认能依次换到其他云/普通节点，最终才用 mcdn/PCDN；失败不无限加载。只记录脱敏主机或错误，不复制完整签名 URL。
3. 音轨响应缓慢时快速换集/关闭，应保持 UI 响应，无旧音轨、重复就绪或新分集误报。
4. 直播正常起播、切清晰度、线路失败回退及重新连接。若普通 HLS 可用而 FLV 仅有 P2P，应先使用 HLS。

## 验收与归档（2026-09-19）

- 用户确认 `fix-scheduler-registration-and-cdn-priority` 已完成 apply，并在 Windows 上手测 pass，授权同步归档。
- 已同步 `video-playback-window`、`live-ui` 的点播完整备选与直播跨轨 CDN 优先要求，保留既有 CC 字幕等规格。
- 原 Windows XML 修复已由同日归档的 `replace-windows-scheduler-with-service` 替代，不恢复旧任务计划实现。
- 归档后代理复核：macOS 应用与回归目标构建通过，CTest **41/41**、OpenSpec 全量严格校验 **29/29**、差异检查通过；Windows 验收依据为上述用户反馈。
