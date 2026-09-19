#ifndef TOVIEW_API_H
#define TOVIEW_API_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "core/BilibiliApiClient.h"
#include "core/HistoryModels.h"

// 稍后再看(Watch-later)域端点(统一管线之上)。端点参考:
// b3/bilibili-api-collect/docs/historytoview/toview.md(GET /x/v2/history/toview,
// Cookie 鉴权,无 WBI)。文档标注列表上限 100,但线上端点单次返回全量快照
// (2026-09-03 实测:count=610 == list.length)—— 本模块不对容量上限做假设,
// 也不发送分页参数。
//
// PGC 条目(番剧/电影/综艺)是文档未记载但线上存在的形态:pgc_label 非空且
// redirect_url 指向 bangumi/play/ep<id>,同时条目根级仍带分集 cid —— 与历史侧 PGC
// 播放路径消费的是同一对 epid+cid。
class ToviewApi {
   public:
    // 单次请求拉取全量稍后再看列表(新添加在前 —— 保持响应原序)。
    static QList<HistoryItem> fetchAll(BilibiliApiClient &api, const QString &cookie);

    // 从条目原始 JSON 定位稍后再看 PGC 条目的分集(epid + cid)。这也是播放器客户端
    // PGC 定位探测消费的第三种识别形态(与历史侧、动态 feed 形态并列)。
    static bool tryParsePgcLocation(const QString &rawJson, qint64 *epId = nullptr,
                                    qint64 *cid = nullptr);

    // 条目原始 JSON 标记稿件已消失(state < 0,如已删除/锁定)时为真。Core 模型保留
    // 原标题;UI 层替换为本地化占位文案。
    static bool isInvalidEntry(const QString &rawJson);

    static QList<HistoryItem> parseList(const QJsonObject &root);
    static HistoryItem parseItem(const QJsonObject &element);

   private:
    static bool tryParsePgcLocation(const QJsonObject &element, qint64 *epId, qint64 *cid);
    static QString buildArchiveLink(qint64 aid, const QString &bvid);
};

#endif  // TOVIEW_API_H
