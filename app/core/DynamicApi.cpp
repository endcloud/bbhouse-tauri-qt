#include "core/DynamicApi.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <QUrlQuery>

#include "core/JsonHelpers.h"

namespace {
constexpr const char *kFeedAllEndpoint =
        "https://api.bilibili.com/x/polymer/web-dynamic/v1/feed/all";
}  // namespace

DynamicFeedPage DynamicApi::fetchFeed(BilibiliApiClient &api, const QString &cookie,
                                      const QString &offset) {
    const auto envelope = api.get(buildFeedUri(offset), cookie);
    return parseFeed(envelope.root, QString::fromUtf8(envelope.raw));
}

QUrl DynamicApi::buildFeedUri(const QString &offset) {
    QUrl url(kFeedAllEndpoint);
    QUrlQuery query;
    query.addQueryItem("platform", "web");
    // 选用新版 opus 形态的图文动态载荷
    query.addQueryItem("features", "itemOpusStyle");
    if (!offset.trimmed().isEmpty()) {
        query.addQueryItem("offset", offset);
    }
    url.setQuery(query);
    return url;
}

DynamicFeedPage DynamicApi::parseFeed(const QJsonObject &root, const QString &rawJson) {
    DynamicFeedPage page;
    page.rawJson = rawJson;

    const QJsonObject data = jh::getChild(root, "data");
    if (data.isEmpty()) return page;

    const QJsonArray list = jh::getChildArray(data, "items");
    for (const QJsonValue &value : list) {
        if (value.isObject()) page.items.append(parseItem(value.toObject()));
    }

    page.offset = jh::getString(data, "offset");
    page.hasMore = jh::getBool(data, "has_more");
    return page;
}

DynamicFeedItem DynamicApi::parseItem(const QJsonObject &element) {
    const bool isForward = jh::getString(element, "type") == "DYNAMIC_TYPE_FORWARD";

    // 生效 major / 分类来源:转发自身不带 major,按原动态的 major 分类
    // (仅取一层 —— 转发的转发或缺 major 一律落入 Other)。
    QJsonObject major = findMajor(element);
    QJsonObject authorSource = element;
    QJsonObject classifySource = element;
    if (isForward && element.value("orig").isObject()) {
        const QJsonObject orig = element.value("orig").toObject();
        const QJsonObject origMajor = findMajor(orig);
        if (!origMajor.isEmpty()) {
            major = origMajor;
        }
        authorSource = orig;
        classifySource = orig;
    }

    const QString majorType = jh::getString(major, "type");
    const QString itemType = jh::getString(classifySource, "type");
    DynamicFeedItem item;
    item.id = jh::getString(element, "id_str");
    item.majorType = majorType;
    item.category = mapCategory(itemType, majorType);
    item.isForward = isForward;
    // 作者行与时间戳都保持动态发布者自身(feed 顺序)。
    item.authorName = getAuthorName(element);
    item.authorMid = getAuthorMid(element);
    item.authorFaceUrl = jh::normalizeImageUrlLenient(jh::getString(
            jh::getChild(jh::getChild(element, "modules"), "module_author"), "face"));
    item.pubTs = getPubTs(element);
    item.rawJson = QString::fromUtf8(QJsonDocument(element).toJson(QJsonDocument::Compact));

    fillContent(item, major, majorType);

    // 专栏如今走 opus 载荷(major = MAJOR_TYPE_OPUS + opus jump_url),条目级 basic 块
    // 里的 cv id 是唯一的专栏权威锚点 —— 优先规范 read/cv 地址而非泛化 opus 链接。
    if (item.category == DynamicCategory::Article) {
        const QString rid = jh::getString(jh::getChild(classifySource, "basic"), "rid_str");
        if (!rid.trimmed().isEmpty()) {
            item.linkUrl = "https://www.bilibili.com/read/cv" + rid;
        }
    }

    // major 无标题时回退动态自身文本(纯文本转发、原动态失效、未知 major)。
    if (item.title.trimmed().isEmpty()) {
        item.title = jh::firstNonEmpty(getDescText(element), getDescText(authorSource));
    }

    if (item.summary.trimmed().isEmpty() && isForward) {
        item.summary = getDescText(element);
    }

    // 失效 = 服务端明示(MAJOR_TYPE_NONE),或完全无可展示内容。纯文本动态没有
    // major 但完全有效。
    item.isUnavailable = majorType == "MAJOR_TYPE_NONE" ||
            (item.title.trimmed().isEmpty() && item.summary.trimmed().isEmpty() &&
             item.coverUrl.trimmed().isEmpty());

    if (item.linkUrl.trimmed().isEmpty()) {
        item.linkUrl = item.id.trimmed().isEmpty()
                ? QStringLiteral("https://www.bilibili.com/")
                : "https://t.bilibili.com/" + item.id;
    }

    return item;
}

// 分类判定:条目级 type 对专栏优先 —— 专栏以 opus 形态到达(major = MAJOR_TYPE_OPUS
// 且带 opus jump_url),仅凭 major 无法与图文动态区分(2026-09-03 对线上 feed 数据
// 验证);其余按生效 major type 映射。
DynamicCategory DynamicApi::mapCategory(const QString &itemType, const QString &majorType) {
    if (itemType == "DYNAMIC_TYPE_ARTICLE") return DynamicCategory::Article;
    if (majorType == "MAJOR_TYPE_ARCHIVE" || majorType == "MAJOR_TYPE_UGC_SEASON")
        return DynamicCategory::Video;
    if (majorType == "MAJOR_TYPE_PGC") return DynamicCategory::Pgc;
    if (majorType == "MAJOR_TYPE_LIVE" || majorType == "MAJOR_TYPE_LIVE_RCMD")
        return DynamicCategory::Live;
    if (majorType == "MAJOR_TYPE_OPUS" || majorType == "MAJOR_TYPE_DRAW")
        return DynamicCategory::Post;
    if (majorType == "MAJOR_TYPE_ARTICLE") return DynamicCategory::Article;  // 旧版 pre-opus 形态,保留兜底
    return DynamicCategory::Other;
}

void DynamicApi::fillContent(DynamicFeedItem &item, const QJsonObject &major,
                             const QString &majorType) {
    if (major.isEmpty()) {
        return;
    }

    if (majorType == "MAJOR_TYPE_ARCHIVE" || majorType == "MAJOR_TYPE_UGC_SEASON") {
        const QJsonObject body =
                jh::getChild(major, majorType == "MAJOR_TYPE_ARCHIVE" ? "archive" : "ugc_season");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "desc");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.aid = jh::getInt64(body, "aid");
        item.durationSeconds = parseDurationText(jh::getString(body, "duration_text"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
        if (item.linkUrl.trimmed().isEmpty()) {
            const QString bvid = jh::getString(body, "bvid");
            item.linkUrl = !bvid.trimmed().isEmpty()
                    ? "https://www.bilibili.com/video/" + bvid
                    : (item.aid > 0
                               ? "https://www.bilibili.com/video/av" + QString::number(item.aid)
                               : QString());
        }
    } else if (majorType == "MAJOR_TYPE_PGC") {
        const QJsonObject body = jh::getChild(major, "pgc");
        item.title = jh::getString(body, "title");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
        if (item.linkUrl.trimmed().isEmpty()) {
            const qint64 epid = jh::getInt64(body, "epid");
            item.linkUrl = epid > 0
                    ? "https://www.bilibili.com/bangumi/play/ep" + QString::number(epid)
                    : QString();
        }
    } else if (majorType == "MAJOR_TYPE_ARTICLE") {
        const QJsonObject body = jh::getChild(major, "article");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "desc");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::firstArrayString(body, "covers"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
        if (item.linkUrl.trimmed().isEmpty()) {
            const qint64 id = jh::getInt64(body, "id");
            item.linkUrl =
                    id > 0 ? "https://www.bilibili.com/read/cv" + QString::number(id) : QString();
        }
    } else if (majorType == "MAJOR_TYPE_OPUS") {
        const QJsonObject body = jh::getChild(major, "opus");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(jh::getChild(body, "summary"), "text");
        // pics 元素的字段名文档未展开 —— 两种取值兼容。
        item.coverUrl =
                jh::normalizeImageUrlLenient(jh::firstArrayObjectString(body, "pics", {"url", "src"}));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
    } else if (majorType == "MAJOR_TYPE_DRAW") {
        const QJsonObject body = jh::getChild(major, "draw");
        item.coverUrl =
                jh::normalizeImageUrlLenient(jh::firstArrayObjectString(body, "items", {"src", "url"}));
    } else if (majorType == "MAJOR_TYPE_LIVE") {
        const QJsonObject body = jh::getChild(major, "live");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "desc_first");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
        if (item.linkUrl.trimmed().isEmpty()) {
            const qint64 id = jh::getInt64(body, "id");
            item.linkUrl =
                    id > 0 ? "https://live.bilibili.com/" + QString::number(id) : QString();
        }
    } else if (majorType == "MAJOR_TYPE_LIVE_RCMD") {
        // 直播间载荷是 `content` 内嵌的 JSON 字符串。
        const QString content = jh::getString(jh::getChild(major, "live_rcmd"), "content");
        fillFromLiveRcmdContent(item, content);
    } else if (majorType == "MAJOR_TYPE_MUSIC") {
        const QJsonObject body = jh::getChild(major, "music");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "label");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
        if (item.linkUrl.trimmed().isEmpty()) {
            const qint64 id = jh::getInt64(body, "id");
            item.linkUrl =
                    id > 0 ? "https://www.bilibili.com/audio/au" + QString::number(id) : QString();
        }
    } else if (majorType == "MAJOR_TYPE_COURSES") {
        const QJsonObject body = jh::getChild(major, "courses");
        item.title = jh::getString(body, "title");
        item.summary = jh::firstNonEmpty(jh::getString(body, "sub_title"),
                                         jh::getString(body, "desc"));
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
    } else if (majorType == "MAJOR_TYPE_COMMON") {
        const QJsonObject body = jh::getChild(major, "common");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "desc");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
    } else if (majorType == "MAJOR_TYPE_MEDIALIST") {
        const QJsonObject body = jh::getChild(major, "medialist");
        item.title = jh::getString(body, "title");
        item.summary = jh::getString(body, "sub_title");
        item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(body, "cover"));
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
    } else if (majorType == "MAJOR_TYPE_UPOWER_COMMON") {
        const QJsonObject body = jh::getChild(major, "upower_common");
        item.title = jh::firstNonEmpty(jh::getString(body, "title"), jh::getString(body, "text"));
        item.summary = jh::getString(body, "desc");
        item.linkUrl = normalizeLinkUrl(jh::getString(body, "jump_url"));
    } else if (majorType == "MAJOR_TYPE_NONE") {
        // 失效动态:`tips` 是服务端提供的占位文案;为空时由 UI 提供本地化兜底。
        item.summary = jh::getString(jh::getChild(major, "none"), "tips");
    }
}

// 从 live_rcmd 的 `content` JSON 字符串里读 标题/封面(尽力而为 —— 载荷解析失败时
// 条目保持文本回退)。
void DynamicApi::fillFromLiveRcmdContent(DynamicFeedItem &item, const QString &content) {
    if (content.trimmed().isEmpty()) {
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 形态变化 —— 保留文本回退。
        return;
    }
    const QJsonObject play = jh::getChild(document.object(), "live_play_info");
    if (play.isEmpty()) {
        return;
    }

    item.title = jh::getString(play, "title");
    item.summary = jh::getString(play, "area_name");
    item.coverUrl = jh::normalizeImageUrlLenient(jh::getString(play, "cover"));
    const qint64 roomId = jh::getInt64(play, "room_id");
    if (roomId > 0) {
        item.linkUrl = "https://live.bilibili.com/" + QString::number(roomId);
    }
}

QJsonObject DynamicApi::findMajor(const QJsonObject &item) {
    const QJsonObject modules = jh::getChild(item, "modules");
    const QJsonObject moduleDynamic = jh::getChild(modules, "module_dynamic");
    return jh::getChild(moduleDynamic, "major");
}

QString DynamicApi::getDescText(const QJsonObject &item) {
    const QJsonObject moduleDynamic = jh::getChild(jh::getChild(item, "modules"), "module_dynamic");
    return jh::getString(jh::getChild(moduleDynamic, "desc"), "text");
}

QString DynamicApi::getAuthorName(const QJsonObject &item) {
    return jh::getString(jh::getChild(jh::getChild(item, "modules"), "module_author"), "name");
}

qint64 DynamicApi::getAuthorMid(const QJsonObject &item) {
    return jh::getInt64(jh::getChild(jh::getChild(item, "modules"), "module_author"), "mid");
}

qint64 DynamicApi::getPubTs(const QJsonObject &item) {
    return jh::getInt64(jh::getChild(jh::getChild(item, "modules"), "module_author"), "pub_ts");
}

int DynamicApi::parseDurationText(const QString &text) {
    if (text.trimmed().isEmpty()) {
        return 0;
    }

    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
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

QString DynamicApi::normalizeLinkUrl(const QString &url) {
    if (url.trimmed().isEmpty()) {
        return QString();
    }
    return url.startsWith(QLatin1String("//")) ? "https:" + url : url;
}
