#include "core/SeasonsApi.h"

#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kSeasonsSeriesListEndpoint =
        "https://api.bilibili.com/x/polymer/web-space/seasons_series_list";
constexpr const char *kSeasonArchivesEndpoint =
        "https://api.bilibili.com/x/polymer/web-space/seasons_archives_list";
constexpr const char *kSeriesArchivesEndpoint = "https://api.bilibili.com/x/series/archives";
}  // namespace

SeasonsSeriesPage SeasonsApi::fetchSeasonsSeriesList(BilibiliApiClient &api, const QString &cookie,
                                                     qint64 mid, int pageNum, int pageSize) {
    if (pageSize > kMaxPageSize) {
        pageSize = kMaxPageSize;
    }

    QUrl url(kSeasonsSeriesListEndpoint);
    QUrlQuery query;
    query.addQueryItem("mid", QString::number(mid));
    query.addQueryItem("page_num", QString::number(pageNum));
    query.addQueryItem("page_size", QString::number(pageSize));
    url.setQuery(query);

    const BilibiliApiClient::Envelope envelope = api.get(url, cookie);
    const QJsonObject lists = jh::getChild(jh::getChild(envelope.root, "data"), "items_lists");

    SeasonsSeriesPage page;
    appendSeasonList(&page.items, jh::getChildArray(lists, "seasons_list"), false);
    appendSeasonList(&page.items, jh::getChildArray(lists, "series_list"), true);
    page.pageTotal = jh::getInt64(jh::getChild(lists, "page"), "total");
    return page;
}

SeasonArchivesPage SeasonsApi::fetchSeasonArchives(BilibiliApiClient &api, const QString &cookie,
                                                   qint64 mid, qint64 seasonId, int pageNum) {
    QUrl url(kSeasonArchivesEndpoint);
    QUrlQuery query;
    query.addQueryItem("mid", QString::number(mid));
    query.addQueryItem("season_id", QString::number(seasonId));
    query.addQueryItem("page_num", QString::number(pageNum));
    query.addQueryItem("page_size", "30");
    url.setQuery(query);
    return parseArchives(api.get(url, cookie));
}

SeasonArchivesPage SeasonsApi::fetchSeriesArchives(BilibiliApiClient &api, const QString &cookie,
                                                   qint64 mid, qint64 seriesId, int pn) {
    QUrl url(kSeriesArchivesEndpoint);
    QUrlQuery query;
    query.addQueryItem("mid", QString::number(mid));
    query.addQueryItem("series_id", QString::number(seriesId));
    query.addQueryItem("only_normal", "true");
    query.addQueryItem("ps", "30");
    query.addQueryItem("pn", QString::number(pn));
    url.setQuery(query);
    return parseArchives(api.get(url, cookie));
}

void SeasonsApi::appendSeasonList(QList<SeasonSummary> *items, const QJsonArray &list,
                                  bool isSeries) {
    for (const QJsonValue &entry : list) {
        if (!entry.isObject()) {
            continue;
        }

        const QJsonObject meta = jh::getChild(entry.toObject(), "meta");
        if (meta.isEmpty()) {
            continue;
        }

        // 合集 meta 带 season_id,系列 meta 带 series_id;标题字段两者都是
        // name(不是 title)
        SeasonSummary summary;
        summary.isSeries = isSeries;
        summary.id = isSeries ? jh::getInt64(meta, "series_id")
                              : jh::getInt64(meta, "season_id");
        summary.name = jh::getString(meta, "name");
        summary.coverUrl = jh::normalizeImageUrlLenient(jh::getString(meta, "cover"));
        summary.total = jh::getInt64(meta, "total");
        items->append(summary);
    }
}

SeasonArchivesPage SeasonsApi::parseArchives(const BilibiliApiClient::Envelope &envelope) {
    const QJsonObject data = jh::getChild(envelope.root, "data");

    SeasonArchivesPage page;
    const QJsonArray list = jh::getChildArray(data, "archives");
    for (const QJsonValue &value : list) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject element = value.toObject();
        SeasonArchive archive;
        archive.aid = jh::getInt64(element, "aid");
        archive.bvid = jh::getString(element, "bvid");
        archive.title = jh::getString(element, "title");
        archive.coverUrl = jh::normalizeImageUrlLenient(jh::getString(element, "pic"));
        // duration 这里以秒到达(与 arc/search 的 "MM:SS" 文本不同)
        archive.durationSeconds = static_cast<int>(jh::getInt64(element, "duration"));
        archive.pubTs = parseTimestamp(element);
        page.archives.append(archive);
    }

    // total 在两个端点都位于 data.page.total(不在 data 根)
    qint64 total = jh::getInt64(jh::getChild(data, "page"), "total");
    if (total <= 0) {
        total = jh::getInt64(jh::getChild(data, "meta"), "total");
    }
    page.total = total;
    return page;
}

qint64 SeasonsApi::parseTimestamp(const QJsonObject &element) {
    // 投稿时间戳:pubdate 优先,ctime 兜底;两者均以数字到达
    const QString text = jh::firstNonEmpty(jh::getString(element, "pubdate"),
                                           jh::getString(element, "ctime"));
    bool ok = false;
    const qint64 ts = text.toLongLong(&ok);
    return ok ? ts : 0;
}
