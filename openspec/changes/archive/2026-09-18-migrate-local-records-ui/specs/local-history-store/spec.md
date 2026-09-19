## ADDED Requirements

### Requirement: 保留已记录观看事件
系统 SHALL 追加真实的新观看时间，MUST NOT 用同步时间伪造新播放事件，MUST NOT 覆盖已存在事件的时间、进度及原始数据，MUST NOT 因云端返回范围缩小而删除旧视频或事件。展示元数据 SHALL 跟随最近真实观看，较旧页面不得倒退最近时间。

#### Scenario: 重抓与新播放
- **WHEN** 已保存视频先被重复同步，再以新 view_at 返回
- **THEN** 重复事件被忽略且原数据不变，新事件独立追加，展示最近时间与次数正确。
