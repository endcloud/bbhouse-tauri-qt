#ifndef HISTORY_API_H
#define HISTORY_API_H

#include <QString>

#include "core/BilibiliApiClient.h"
#include "core/HistoryModels.h"

// 历史域端点(cursor 分页观看历史)。端点参考:
// b3/bilibili-api-collect/docs/historytoview/history.md
class HistoryApi {
   public:
    static BilibiliHistoryPage fetchPage(BilibiliApiClient &api, const QString &cookie,
                                         const HistoryCursor &cursor, int pageSize);

    static QUrl buildUri(const HistoryCursor &cursor, int pageSize);
    static HistoryCursor parseCursor(const QJsonObject &data);
    static HistoryItem parseItem(const QJsonObject &element);
    static QString buildLinkUrl(const QString &uri, const QString &business,
                                const QString &bvid, qint64 oid, qint64 kid);
    static QString findCoverUrl(const QJsonObject &element);
};

#endif  // HISTORY_API_H
