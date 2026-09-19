![banner](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-40-19.png?imageMogr2/format/webp/thumbnail/!50p)

# bbhouse-qt

##  [bbhouse-tauri](https://www.v2ex.com/t/872704) 的精神续作 

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/endcloud/bbhouse-tauri-qt)](https://github.com/endcloud/bbhouse-tauri-qt/releases) ![GitHub Release Date](https://img.shields.io/github/release-date/endcloud/bbhouse-tauri-qt) ![GitHub All Releases](https://img.shields.io/github/downloads/endcloud/bbhouse-tauri-qt/total) ![GitHub stars](https://img.shields.io/github/stars/endcloud/bbhouse-tauri-qt?style=flat) ![GitHub forks](https://img.shields.io/github/forks/endcloud/bbhouse-tauri-qt)

_名称取自常见的『我在B站买了房』的评论_

## 已实现的功能
播放器: 播放列表, 杜比视界与HDR支持, 帧截图, 弹幕, 字幕, 倍速, 下载; 
在线功能: (已关注的)动态与直播, 追番(港澳台), 流行, 特别关注; 历史记录, 稍后再看
请注意: ***无法会普通会员提供超过权限的功能***

## feature 
1. 极简, 以动态流为首页, 默认不展示b博, 没有推荐流
2. 动态流支持筛选 by 标题, up主, 分区, 方便关注爆满的朋友
3. 单独生命周期的播放窗口与列表, 基于mpv后端, 支持杜比视界与HDR
4. 对随机出现的PCDN进行了降权处理 
5. 支持代理播放港澳台番剧
6. 完善的下载管理与本地导入弹幕播放功能
7. 现代化的FluentUI, 明/暗, 多语言支持. 
8. 高性能的Native应用, 采用全国***最小而美的app(微信)***同源技术, qt+cpp 构建二进制
9. 跨平台(目前release: Windows_x64, macOS_arm64)
10. 基于cookie认证, 轻依赖

## 特色功能 
1. 本地特别关注: 解决特别关注有上限的问题, 方便查看UP主空间, 追更
2. 本地历史缓存: 定期同步, 解决云历史有上限的问题

## todo 
- 评论区 
- AI分析与推荐
- mpv的shader插件, 如anime4K 

## vibe 声明 
1. 本项目由于需要FluentUI, port自WinUI版本的同名项目与, port时同步参考了原先的taur项目, 此过程使用 `Zcode GLM-5.3-flash` 完成
2. 多平台编译构建排错, 此过程使用 `Trae Kimi-K3` 完成
3. 细节与优化以及后续, 此过程使用 `Codex GPT-6-Astra` 完成

## 致谢 

### mpv后端的接入
- [Bili.Copilot](https://github.com/Richasy/Bili.Copilot)
- [wiliwili](https://github.com/xfangfang/wiliwili)
### 弹幕实现与优化
- [DanmakuFrostMaster](https://github.com/cotaku/DanmakuFrostMaster)
- [Danmaku](https://github.com/weizhenye/Danmaku)
- [biliLocal](https://github.com/ancientlysine/bililocal)
- [pakku.js](https://github.com/xmcp/pakku.js)
### 在线服务 
- [bilibili-api-collect]()
- [哔哩哔哩-干杯](https://www.bilibili.com/space)
- [Linux.do - 新的理想社区](https://linux.do/)
### 其他 
- 向所有曾star/pr的朋友致谢, 22年的这个项目, 对我个人而言, 在工作和生活中, 都带来了莫大鼓励.  

## 实机截图 
![动态](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-40-42.png?imageMogr2/format/webp/thumbnail/!50p)
![流行](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-40-52.png?imageMogr2/format/webp/thumbnail/!50p)
![直播](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-40-32.png?imageMogr2/format/webp/thumbnail/!50p)
![番剧-港澳台](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-45-06.png?imageMogr2/format/webp/thumbnail/!50p)
![特别关注](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-41-05.png?imageMogr2/format/webp/thumbnail/!50p)
![本地历史缓存](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-42-11.png?imageMogr2/format/webp/thumbnail/!50p)
![播放器](https://cos.endcloud.cn/v2ex/Xnip2026-09-19_14-46-09.png?imageMogr2/format/webp/thumbnail/!50p)