#ifndef CARD_AUTHOR_H
#define CARD_AUTHOR_H

#include <QJsonDocument>

#include "core/HistoryModels.h"
#include "core/JsonHelpers.h"

// 头像保留在已有原始载荷中，旧的本地历史也可直接恢复，无需数据库迁移。
inline QString historyAuthorFaceUrl(const HistoryItem &item) {
    const QJsonObject raw = QJsonDocument::fromJson(item.rawJson.toUtf8()).object();
    return jh::normalizeImageUrlLenient(jh::firstNonEmpty(
            jh::getString(raw, "author_face"),
            jh::getString(jh::getChild(raw, "owner"), "face")));
}

#endif  // CARD_AUTHOR_H
