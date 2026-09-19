# history-sync Specification

## Purpose
定义应用如何凭用户提供的 cookie 访问 B 站云端历史记录 cursor API，并以游标翻页方式把云端全部可见历史（约 1000 条上限）拉取到本地，包括认证失败处理、限速与终止条件。

## Requirements

### Requirement: Cookie 凭据加载与校验

系统 SHALL 从仓库根目录的 `bilibili.cookie.txt` 读取 cookie，且该文件 MUST 包含 `SESSDATA` 字段。文件缺失、为空或缺 `SESSDATA` 时，同步 MUST 以明确的中文错误信息终止，不得发起任何 API 请求。

#### Scenario: cookie 文件缺失
- **WHEN** 仓库根目录不存在 `bilibili.cookie.txt` 且用户点击"同步"
- **THEN** 同步终止，状态栏提示"未找到 bilibili.cookie.txt"

#### Scenario: cookie 缺少 SESSDATA
- **WHEN** cookie 文件内容不含 `SESSDATA=`
- **THEN** 同步终止，状态栏提示"cookie 文件中没有找到 SESSDATA"

### Requirement: 游标分页全量爬取

系统 SHALL 调用 `GET https://api.bilibili.com/x/web-interface/history/cursor`，以每页 30 条（`ps=30`、`type=all`）从最新记录开始，用响应返回的游标（`max`/`view_at`/`business`）逐页向过去翻页，直到命中终止条件。每个请求 MUST 携带 cookie 以及浏览器形态的 `User-Agent` 与 `Referer` 请求头。相邻两页请求之间 MUST 至少间隔 1 秒。

#### Scenario: 首次全量同步
- **WHEN** 本地缓存为空且 cookie 有效
- **THEN** 系统从第一页开始逐页请求，直到云端历史翻尽（返回空页或游标终止），读取条数收敛于云端上限（约 1000 条）

#### Scenario: 翻页限速
- **WHEN** 一页成功落库并准备请求下一页
- **THEN** 系统等待 1 秒后才发起下一页请求，且状态栏展示当前页进度与累计新增条数

### Requirement: 爬取终止条件

爬取循环 MUST 在以下任一情形立即停止，不得死循环：当前页返回 0 条记录；响应游标缺少 `business` 字段；响应游标与上一页完全相同。

#### Scenario: 云端历史翻尽
- **WHEN** 某页返回的列表为空
- **THEN** 爬取正常结束，同步记为 success

#### Scenario: 游标不再前进
- **WHEN** 响应游标 (max, view_at, business) 与上一页一致
- **THEN** 爬取停止，不再次请求相同游标

### Requirement: 登录失效处理

API 返回 `code=-101` 时，系统 MUST 将其识别为登录失效（区别于其他错误），以"登录失效"前缀向用户展示，并将本次同步在审计中记为 failed。

#### Scenario: SESSDATA 过期
- **WHEN** cookie 中 SESSDATA 已失效，接口返回 code=-101
- **THEN** 状态栏显示"登录失效: 账号未登录或 SESSDATA 已失效"，不继续翻页

### Requirement: 同步结果反馈

每次同步结束时，系统 MUST 向用户展示汇总：请求页数、读取条数、新增观看记录条数、已存在条数。

#### Scenario: 同步完成提示
- **WHEN** 全量爬取正常结束
- **THEN** 状态栏显示"同步完成：请求 N 页，读取 M 条，新增 X 条，已存在 Y 条"，列表回到第一页并展示新数据

### Requirement: 原生同步互斥与异常终止
手动与定时同步 SHALL 共用 C++/Qt 爬虫，通过数据库路径级锁防止重叠请求；相邻页 SHALL 至少间隔 1s，等待支持取消；空 data/list SHALL 正常结束，循环游标 SHALL 终止并呈现异常，不伪装已翻尽。权限或 API 失败 SHALL 保留成功页并在可写数据库中审计。

#### Scenario: 请求重叠
- **WHEN** 同一数据库正在同步时又启动另一次同步
- **THEN** 后者明确报告已有任务进行中，不额外访问历史 API。

#### Scenario: 跨多页游标循环
- **WHEN** 返回游标回到之前已请求的游标
- **THEN** 停止请求并报告异常，已提交页保持完整。
