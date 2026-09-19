#include "core/ToviewApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kToviewEndpoint = "https://api.bilibili.com/x/v2/history/toview";

// 容忍剧集页 URL 带查询串(如 "?theme=movie")
const QRegularExpression kEpisodeIdRegex(QStringLiteral("bangumi/play/ep(\\d+)"));
}  // namespace

QList<HistoryItem> ToviewApi::fetchAll(BilibiliApiClient &api, const QString &cookie) {
    const auto envelope = api.get(QUrl(kToviewEndpoint), cookie);
    return parseList(envelope.root);
}

bool ToviewApi::tryParsePgcLocation(const QString &rawJson, qint64 *epId, qint64 *cid) {
    if (epId) *epId = 0;
    if (cid) *cid = 0;
    if (rawJson.trimmed().isEmpty()) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    return tryParsePgcLocation(document.object(), epId, cid);
}

bool ToviewApi::isInvalidEntry(const QString &rawJson) {
    if (rawJson.trimmed().isEmpty()) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    return jh::getInt64(document.object(), "state") < 0;
}

QList<HistoryItem> ToviewApi::parseList(const QJsonObject &root) {
    QList<HistoryItem> items;
    const QJsonObject data = jh::getChild(root, "data");
    if (data.isEmpty()) {
        return items;
    }

    const QJsonArray list = jh::getChildArray(data, "list");
    for (const QJsonValue &value : list) {
        if (value.isObject()) {
            items.append(parseItem(value.toObject()));
        }
    }
    return items;
}

HistoryItem ToviewApi::parseItem(const QJsonObject &element) {
    const qint64 aid = jh::getInt64(element, "aid");
    const QString bvid = jh::getString(element, "bvid");
    const QString redirectUrl = jh::getString(element, "redirect_url");
    const bool isPgc = !jh::getString(element, "pgc_label").trimmed().isEmpty();

    // 与历史域相同的 `business:oid:kid` 约定,使 store 键下持久化的播放进度跨页可续:
    // 稿件条目共用完全相同的 archive:{aid}:{aid} 键;PGC 条目此处没有 season id,键只能
    // 尽力而为(pgc:{epid}:0,兜底唯一键 pgc:{aid}:{aid})。
    QString business;
    QString videoKey;
    QString linkUrl;
    if (isPgc) {
        business = "pgc";
        linkUrl = redirectUrl.trimmed().isEmpty() ? buildArchiveLink(aid, bvid) : redirectUrl;
        qint64 epId = 0;
        qint64 cid = 0;
        videoKey = tryParsePgcLocation(element, &epId, &cid) && epId > 0
                ? "pgc:" + QString::number(epId) + ":0"
                : "pgc:" + QString::number(aid) + ":" + QString::number(aid);
    } else {
        business = "archive";
        linkUrl = buildArchiveLink(aid, bvid);
        videoKey = "archive:" + QString::number(aid) + ":" + QString::number(aid);
    }

    const QJsonObject owner = jh::getChild(element, "owner");
    HistoryItem item;
    item.videoKey = videoKey;
    item.kid = aid;
    item.oid = aid;
    item.business = business;
    item.title = jh::getString(element, "title");
    item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(element, "pic"));
    item.authorName = jh::getString(owner, "name");
    item.authorMid = jh::getInt64(owner, "mid");
    item.viewAt = jh::getInt64(element, "add_at");
    // 负进度(-1)上游语义是"已看完";原样保留 —— 卡片视图模型把 <=0 一律按"无进度"渲染。
    item.progress = static_cast<int>(jh::getInt64(element, "progress"));
    item.duration = static_cast<int>(jh::getInt64(element, "duration"));
    item.linkUrl = linkUrl;
    item.rawJson = QString::fromUtf8(QJsonDocument(element).toJson(QJsonDocument::Compact));
    return item;
}

bool ToviewApi::tryParsePgcLocation(const QJsonObject &element, qint64 *epId, qint64 *cid) {
    if (epId) *epId = 0;
    if (cid) *cid = 0;

    // 稍后再看 PGC 特征:非空 pgc_label + 剧集页跳转地址。
    if (jh::getString(element, "pgc_label").trimmed().isEmpty()) {
        return false;
    }

    const QString redirectUrl = jh::getString(element, "redirect_url");
    if (redirectUrl.trimmed().isEmpty()) {
        return false;
    }

    const QRegularExpressionMatch match = kEpisodeIdRegex.match(redirectUrl);
    bool ok = false;
    const qint64 parsedEpId = match.captured(1).toLongLong(&ok);
    if (!match.hasMatch() || !ok) {
        return false;
    }
    if (epId) *epId = parsedEpId;

    const qint64 parsedCid = jh::getInt64(element, "cid");
    if (cid) *cid = parsedCid;
    return parsedEpId > 0 && parsedCid > 0;
}

QString ToviewApi::buildArchiveLink(qint64 aid, const QString &bvid) {
    return !bvid.trimmed().isEmpty()
            ? "https://www.bilibili.com/video/" + bvid
            : "https://www.bilibili.com/video/av" + QString::number(aid);
}
