## ADDED Requirements

### Requirement: 原生同步互斥与异常终止
手动与定时同步 SHALL 共用 C++/Qt 爬虫，通过数据库路径级锁防止重叠请求；相邻页 SHALL 至少间隔 1s，等待支持取消；空 data/list SHALL 正常结束，循环游标 SHALL 终止并呈现异常，不伪装已翻尽。权限或 API 失败 SHALL 保留成功页并在可写数据库中审计。

#### Scenario: 请求重叠
- **WHEN** 同一数据库正在同步时又启动另一次同步
- **THEN** 后者明确报告已有任务进行中，不额外访问历史 API。

#### Scenario: 跨多页游标循环
- **WHEN** 返回游标回到之前已请求的游标
- **THEN** 停止请求并报告异常，已提交页保持完整。
