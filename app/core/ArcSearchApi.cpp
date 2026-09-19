#include "core/ArcSearchApi.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

#include "core/ApiErrors.h"
#include "core/JsonHelpers.h"
#include "core/WbiSigner.h"

namespace {
constexpr const char *kArcSearchEndpoint = "https://api.bilibili.com/x/space/wbi/arc/search";

const char *orderText(ArcSortOrder order) {
    switch (order) {
        case ArcSortOrder::Click:
            return "click";
        case ArcSortOrder::Stow:
            return "stow";
        case ArcSortOrder::Pubdate:
            break;
    }
    return "pubdate";
}

// 对齐原 DynamicApi.ParseDurationText:"MM:SS" / "H:MM:SS" 文本 → 秒;
// 非 2-3 段、任何一段非整数或为负 → 0。
int parseDurationText(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }

    const QStringList parts = trimmed.split(':');
    if (parts.size() < 2 || parts.size() > 3) {
        return 0;
    }

    int total = 0;
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok || value < 0) {
            return 0;
        }
        total = total * 60 + value;
    }
    return total;
}
}  // namespace

ArcSearchPage ArcSearchApi::fetch(BilibiliApiClient &api, const QString &cookie, qint64 mid,
                                  ArcSortOrder order, qint64 tid, int pn) {
    QMap<QString, QString> parameters;
    parameters.insert("mid", QString::number(mid));
    parameters.insert("order", orderText(order));

    if (tid > 0) {
        parameters.insert("tid", QString::number(tid));
    }

    parameters.insert("pn", QString::number(pn));
    parameters.insert("ps", QString::number(kPageSize));

    // WBI 必需:先取 (w_rid, wts) 再并参
    const QPair<QString, QString> signature = WbiSigner::sign(api, cookie, parameters);
    parameters.insert("w_rid", signature.first);
    parameters.insert("wts", signature.second);

    QUrl url(kArcSearchEndpoint);
    QUrlQuery query;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    url.setQuery(query);

    // WBI 端点常见 HTTP 412 风控:最多尝试 3 次,间隔 600ms * attempt(同原实现)
    constexpr int kMaxAttempts = 3;
    for (int attempt = 1;; ++attempt) {
        try {
            const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
            return parse(envelope.root);
        } catch (const ApiError &error) {
            if (error.httpStatus() == 412 && attempt < kMaxAttempts) {
                QThread::msleep(600 * attempt);
                continue;
            }
            throw;
        }
    }
}

ArcSearchPage ArcSearchApi::parse(const QJsonObject &root) {
    const QJsonObject data = jh::getChild(root, "data");
    const QJsonObject list = jh::getChild(data, "list");

    ArcSearchPage page;
    const QJsonArray vlist = jh::getChildArray(list, "vlist");
    for (const QJsonValue &value : vlist) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject element = value.toObject();
        ArcVideo video;
        const QString lengthText = jh::getString(element, "length");
        video.aid = jh::getInt64(element, "aid");
        video.bvid = jh::getString(element, "bvid");
        video.title = jh::getString(element, "title");
        video.coverUrl = jh::normalizeImageUrlLenient(jh::getString(element, "pic"));
        video.created = jh::getInt64(element, "created");
        video.play = jh::getInt64(element, "play");
        video.review = jh::getInt64(element, "video_review");
        video.lengthText = lengthText;
        video.durationSeconds = parseDurationText(lengthText);
        page.videos.append(video);
    }

    // tlist 是以 tid 为键的对象(不是数组):{ "tid": {count, name, tid}, ... }
    const QJsonObject tlist = jh::getChild(list, "tlist");
    for (auto it = tlist.constBegin(); it != tlist.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }

        const QJsonObject entry = it.value().toObject();
        ArcPartition partition;
        partition.tid = jh::getInt64(entry, "tid");
        partition.name = jh::getString(entry, "name");
        partition.count = jh::getInt64(entry, "count");
        page.partitions.append(partition);
    }

    std::sort(page.partitions.begin(), page.partitions.end(),
              [](const ArcPartition &a, const ArcPartition &b) { return a.count > b.count; });

    page.total = jh::getInt64(jh::getChild(data, "page"), "count");
    return page;
}
