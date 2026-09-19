# local-history-store Specification

## Purpose
定义云端历史的本地 SQLite 持久化契约：以"视频"与"观看事件"两级模型存储数据，保证重同步幂等且每次观看时间戳独立保留，同时记录同步审计、每页原始响应，并支持 JSON 导出与旧数据一次性迁移。

## Requirements

### Requirement: 两级数据模型

存储 MUST 区分两个粒度：每个不同的视频/直播间/文章等条目（以稳定标识 `video_key = business:oid:kid`）在 `videos` 中至多一行，承载展示元数据；每次观看事件在 `view_records` 中一行，以 `(video_key, view_at)` 唯一。`videos` 的展示列（最近观看时间、进度、副标题、原始 JSON）MUST 镜像该视频最新一次观看的数据。`videos` MUST 缓存 `view_count`（该视频的观看事件总数）并按 `latest_view_at` 降序支持分页查询。

#### Scenario: 同一视频多次观看
- **WHEN** 用户在不同时间戳观看了同一视频并完成两次同步
- **THEN** `videos` 中该视频仍只有一行，`view_records` 中有两行，`view_count` 为 2

#### Scenario: 按时间分页浏览
- **WHEN** 客户端请求第 N 页（每页 30 条）
- **THEN** 返回按最近观看时间降序排列的 30 个视频，且每个视频附带其全部观看时间戳

### Requirement: 重同步幂等

同一 `(video_key, view_at)` 的观看事件重复入库时 MUST 被忽略（计为"已存在"），不得产生重复行；只有新的观看时间戳才产生新行（计为"新增"）。

#### Scenario: 无新观看的重复同步
- **WHEN** 云端历史自上次同步以来没有变化，用户再次全量同步
- **THEN** 新增为 0，全部条目计为已存在，数据库行数不变

### Requirement: 同步审计

每次同步 MUST 在 `sync_runs` 中记录生命周期（running → success/cancelled/failed）、起止时间与统计（页数/读取/新增/已存在）。每页 MUST 在 `cursor_snapshots` 中保存当页游标与完整原始 JSON 响应，供事后回放审计。

#### Scenario: 同步失败留痕
- **WHEN** 同步中途因网络错误失败
- **THEN** 对应 `sync_runs` 行状态为 failed 并保存错误信息，已成功落库的页保留其 cursor 快照

### Requirement: JSON 导出

同步成功后系统 MUST 把全部视频（含每个视频的全部观看时间戳、观看次数与最近观看元数据）导出为 UTF-8 JSON 文件，写入本地缓存目录。

#### Scenario: 导出文件生成
- **WHEN** 一次同步成功结束
- **THEN** 缓存目录中的导出 JSON 被更新，`count` 等于视频总数，每个条目包含 `view_ats` 时间戳数组

### Requirement: 旧数据一次性迁移

首次在新模型上启动且存在旧版扁平表数据时，系统 MUST 在事务中把旧数据迁入两级模型（每条旧行恰好对应一个观看事件），迁移后旧表 MUST 原样保留作备份；`videos` 已有数据时 MUST NOT 重复迁移。

#### Scenario: 旧库升级
- **WHEN** 用户带着旧版 `history_items` 数据的数据库启动新版应用
- **THEN** 数据一次性迁入 `videos` 与 `view_records`，观看次数统计正确，`history_items` 表内容不变

### Requirement: 同步运行触发来源

每次同步运行 MUST 在审计表中记录触发来源（`manual` 手动 / `scheduled` 定时服务）；读取运行记录时， feature 引入前的历史行 MUST 按手动来源解释。触发来源是运行日志展示与统计口径的数据契约。

#### Scenario: 手动与服务运行可区分
- **WHEN** 用户先在主应用手动同步一次，随后服务定时同步一次
- **THEN** 审计表中两条运行记录的触发来源分别为 manual 与 scheduled

#### Scenario: 历史记录回溯解释
- **WHEN** 控制界面读取 feature 上线前产生的历史运行记录
- **THEN** 这些记录展示为手动触发，读取不报错

### Requirement: 保留已记录观看事件
系统 SHALL 追加真实的新观看时间，MUST NOT 用同步时间伪造新播放事件，MUST NOT 覆盖已存在事件的时间、进度及原始数据，MUST NOT 因云端返回范围缩小而删除旧视频或事件。展示元数据 SHALL 跟随最近真实观看，较旧页面不得倒退最近时间。

#### Scenario: 重抓与新播放
- **WHEN** 已保存视频先被重复同步，再以新 view_at 返回
- **THEN** 重复事件被忽略且原数据不变，新事件独立追加，展示最近时间与次数正确。
