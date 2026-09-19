#include "core/PlayerApi.h"

#include <QJsonArray>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>  // Windows 版 Qt 捆绑的私有 zlib 头
#else
#include <zlib.h>        // macOS/Linux 走系统 zlib
#endif
#include <optional>

#include "core/ApiErrors.h"
#include "core/JsonHelpers.h"
#include "core/MediaUrlPolicy.h"
#include "core/WbiSigner.h"

namespace {
constexpr const char *kPagelistEndpoint = "https://api.bilibili.com/x/player/pagelist";
constexpr const char *kPlayUrlEndpoint = "https://api.bilibili.com/x/player/wbi/playurl";
constexpr const char *kPgcPlayUrlEndpoint = "https://api.bilibili.com/pgc/player/web/playurl";
constexpr const char *kPgcSeasonEndpoint = "https://api.bilibili.com/pgc/view/web/season";
constexpr const char *kPlayerInfoEndpoint = "https://api.bilibili.com/x/player/wbi/v2";
constexpr const char *kNavEndpoint = "https://api.bilibili.com/x/web-interface/nav";
constexpr const char *kDanmakuEndpoint = "https://comment.bilibili.com/";

QUrl buildUrl(const char *endpoint, const QMap<QString, QString> &params) {
    QUrl url(endpoint);
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    url.setQuery(query);
    return url;
}

// 追加 WBI 签名两参(对应 C#:var (wRid, wts) = WbiSigner.Sign(...); params[w_rid/wts]=...)
void appendWbiParams(QMap<QString, QString> *params, BilibiliApiClient &api,
                     const QString &cookie) {
    const QPair<QString, QString> signedPair =
            WbiSigner::sign(api, cookie, *params);
    params->insert("w_rid", signedPair.first);
    params->insert("wts", signedPair.second);
}

// 单流全量解压。windowBits:MAX_WBITS=zlib 壳,-MAX_WBITS=raw deflate,
// 16+MAX_WBITS=gzip。流损坏/截断返回 false,由调用方决定抛错或回退。
bool inflateAll(const QByteArray &src, int windowBits, QByteArray *out) {
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src.constData()));
    zs.avail_in = static_cast<uInt>(src.size());
    if (inflateInit2(&zs, windowBits) != Z_OK) return false;

    char buffer[65536];
    int ret = Z_OK;
    while (true) {
        zs.next_out = reinterpret_cast<Bytef *>(buffer);
        zs.avail_out = sizeof(buffer);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        out->append(buffer, sizeof(buffer) - zs.avail_out);
        if (ret == Z_STREAM_END) break;
        if (ret == Z_BUF_ERROR) {
            // 输入耗尽仍未见流结束标记 —— 截断
            inflateEnd(&zs);
            return false;
        }
    }
    inflateEnd(&zs);
    return true;
}
}  // namespace

// ---- 分 P ----

PlayerApi::VideoPage PlayerApi::getFirstPage(BilibiliApiClient &api, qint64 aid,
                                             const QString &cookie) {
    QMap<QString, QString> params;
    params.insert("aid", QString::number(aid));
    const auto envelope = api.get(buildUrl(kPagelistEndpoint, params), cookie);

    const QJsonArray data = jh::getChildArray(envelope.root, "data");
    if (data.isEmpty()) {
        throw ApiError(0, Loc::get("未找到分 P 信息，视频可能已失效。"), 0);
    }

    const QJsonObject first = data.at(0).toObject();
    VideoPage page;
    page.cid = jh::getInt64(first, "cid");
    page.page = static_cast<int>(jh::getInt64(first, "page"));
    page.part = jh::getString(first, "part");
    return page;
}

// ---- MP4 durl(fnval=1)----

PlayerApi::PlayUrl PlayerApi::getPlayUrl(BilibiliApiClient &api, qint64 aid, qint64 cid,
                                         const QString &cookie, int qn) {
    try {
        return getPlayUrlCore(api, aid, cid, cookie, qn);
    } catch (const ApiUnauthorizedError &) {
        throw;  // C#:UnauthorizedAccessException 不属于 InvalidOperationException,不触发回退
    } catch (const ApiError &) {
        if (qn == 64) throw;
        // qn≠64 的每次尝试都失败时以 64 重试一次:清晰度选择不能把播放搞挂
        return getPlayUrlCore(api, aid, cid, cookie, 64);
    }
}

PlayerApi::PlayUrl PlayerApi::getPlayUrlCore(BilibiliApiClient &api, qint64 aid, qint64 cid,
                                             const QString &cookie, int qn) {
    const char *attempts[] = {"pc", "html5"};  // 两形态均带 high_quality=1

    std::optional<ApiError> lastError;
    for (const char *platform : attempts) {
        QMap<QString, QString> params;
        params.insert("avid", QString::number(aid));
        params.insert("cid", QString::number(cid));
        params.insert("qn", QString::number(qn));
        params.insert("fnval", "1");
        params.insert("fnver", "0");
        params.insert("fourk", "1");
        params.insert("platform", platform);
        params.insert("high_quality", "1");
        appendWbiParams(&params, api, cookie);

        try {
            const auto envelope = api.get(buildUrl(kPlayUrlEndpoint, params), cookie);
            const QJsonValue dataValue = envelope.root.value("data");
            if (!dataValue.isObject()) {
                throw ApiError(0, Loc::get("播放地址接口返回缺少 data。"), 0);
            }
            return parseDurlFromData(dataValue.toObject());
        } catch (const ApiUnauthorizedError &) {
            throw;
        } catch (const ApiError &ex) {
            lastError = ex;  // html5 形态回退前记住 pc 形态的失败
        }
    }

    if (lastError.has_value()) throw lastError.value();
    throw ApiError(0, Loc::get("播放地址解析失败。"), 0);
}

PlayerApi::PlayUrl PlayerApi::parseDurlFromData(const QJsonObject &data) {
    const int quality = static_cast<int>(jh::getInt64(data, "quality"));
    const QList<QualityOption> options = parseQualityOptions(data);
    QString qualityLabel;
    for (const QualityOption &option : options) {
        if (option.qn == quality) {
            qualityLabel = option.label;
            break;
        }
    }
    if (qualityLabel.trimmed().isEmpty()) {
        for (const QualityOption &option : options) {
            if (!option.label.trimmed().isEmpty()) {
                qualityLabel = option.label;
                break;
            }
        }
    }

    const QJsonArray durl = jh::getChildArray(data, "durl");
    if (!durl.isEmpty()) {
        const QJsonObject first = durl.at(0).toObject();
        QStringList candidates{jh::getString(first, "url")};
        for (const char *key : {"backup_url", "backupUrl"}) {
            for (const QJsonValue &value : first.value(key).toArray())
                candidates.append(value.toString());
        }
        const QStringList urls = MediaUrlPolicy::ordered(candidates);
        if (!urls.isEmpty()) {
            PlayUrl out;
            out.urls = urls;
            out.url = urls.first();
            out.quality = quality;
            out.qualityLabel = qualityLabel;
            out.availableQualities = options;
            return out;
        }
    }

    throw ApiError(0, Loc::get("返回内容中没有可用的 MP4 播放地址（可能需要更高权限或视频已失效）。"), 0);
}

QList<PlayerApi::QualityOption> PlayerApi::parseQualityOptions(const QJsonObject &data) {
    // accept_quality 与 accept_description 按下标对齐;qn 不可用的条目丢弃。
    // 清单镜像 API 提供的档位 —— durl 路径仍可能回退到低于所选档位。
    const QJsonArray qualities = jh::getChildArray(data, "accept_quality");
    if (qualities.isEmpty()) return {};
    const QJsonArray descriptions = jh::getChildArray(data, "accept_description");

    QList<QualityOption> list;
    for (int i = 0; i < qualities.size(); ++i) {
        const QJsonValue element = qualities.at(i);
        int qn = 0;
        if (element.isDouble()) {
            qn = static_cast<int>(element.toDouble());
        } else if (element.isString()) {
            qn = element.toString().toInt();
        }
        if (qn <= 0) continue;

        QualityOption option;
        option.qn = qn;
        option.label = i < descriptions.size() ? descriptions.at(i).toString() : QString();
        list.append(option);
    }
    return list;
}

// ---- DASH(主站,fnval=4048)----

PlayerApi::DashPlayInfo PlayerApi::getDashPlayUrl(BilibiliApiClient &api, qint64 aid,
                                                  qint64 cid, const QString &cookie,
                                                  int preferQn, const QString &preferCodec) {
    QMap<QString, QString> params;
    params.insert("avid", QString::number(aid));
    params.insert("cid", QString::number(cid));
    params.insert("qn", "120");
    params.insert("fnval", "4048");
    params.insert("fnver", "0");
    params.insert("fourk", "1");
    params.insert("platform", "pc");
    appendWbiParams(&params, api, cookie);

    const auto envelope = api.get(buildUrl(kPlayUrlEndpoint, params), cookie);
    const QJsonValue dataValue = envelope.root.value("data");
    const QJsonObject data = dataValue.toObject();
    if (!dataValue.isObject() || !data.contains("dash")) {
        throw ApiError(0, Loc::get("响应中没有 DASH 数据。"), 0);
    }

    return selectDash(data, jh::getChild(data, "dash"), preferQn, preferCodec);
}

// ---- PGC(番剧/电影)----

PlayerApi::PgcDashInfo PlayerApi::getPgcDashPlayUrl(BilibiliApiClient &api, qint64 epId,
                                                    qint64 cid, const QString &cookie,
                                                    int preferQn, const QString &preferCodec) {
    const QJsonObject result = getPgcPlayUrlResult(api, epId, cid, cookie, QStringLiteral("4048"), 120);
    if (!result.contains("dash")) {
        throw ApiError(0, Loc::get("响应中没有 DASH 数据。"), 0);
    }

    PgcDashInfo out;
    out.dash = selectDash(result, jh::getChild(result, "dash"), preferQn, preferCodec);
    out.entitlement = parsePgcEntitlement(result);
    return out;
}

PlayerApi::PgcPlayUrl PlayerApi::getPgcPlayUrl(BilibiliApiClient &api, qint64 epId,
                                               qint64 cid, const QString &cookie, int qn) {
    const QJsonObject result = getPgcPlayUrlResult(api, epId, cid, cookie, QStringLiteral("1"), qn);
    PgcPlayUrl out;
    out.playUrl = parseDurlFromData(result);
    out.entitlement = parsePgcEntitlement(result);
    return out;
}

qint64 PlayerApi::getPgcEpisodeCid(BilibiliApiClient &api, qint64 epId, const QString &cookie) {
    QMap<QString, QString> params;
    params.insert("ep_id", QString::number(epId));

    QJsonObject root;
    try {
        root = api.get(buildUrl(kPgcSeasonEndpoint, params), cookie).root;
    } catch (const ApiError &ex) {
        // C#:code 非 0 一律返回 0(解析尽力而为,播放不得依赖);传输层失败照抛。
        // 统一管线把信封非零 code(含 -101)映射为带 code 的 ApiError,据此区分。
        if (ex.code() != 0) return 0;
        throw;
    }

    return parsePgcEpisodeCid(jh::getChild(root, "result"), epId);
}

qint64 PlayerApi::parsePgcEpisodeCid(const QJsonObject &result, qint64 epId) {
    if (epId <= 0) return 0;
    const QJsonArray episodes = jh::getChildArray(result, "episodes");
    for (const QJsonValue &value : episodes) {
        if (!value.isObject()) continue;
        const QJsonObject episode = value.toObject();
        if (jh::getInt64(episode, "id") == epId ||
            jh::getInt64(episode, "ep_id") == epId) {
            return jh::getInt64(episode, "cid");
        }
    }
    return 0;
}

QJsonObject PlayerApi::getPgcPlayUrlResult(BilibiliApiClient &api, qint64 epId, qint64 cid,
                                           const QString &cookie, const QString &fnval,
                                           int qn) {
    QMap<QString, QString> params;
    if (epId > 0) params.insert("ep_id", QString::number(epId));
    params.insert("qn", QString::number(qn));
    params.insert("fnval", fnval);
    params.insert("fnver", "0");
    params.insert("fourk", "1");

    // 文档:「ep_id 与 cid 任选其一」:动态 feed 条目只有 epid,发 cid=0 与省略 cid
    // 不等价 —— 参数整体省略。
    if (cid > 0) {
        params.insert("cid", QString::number(cid));
    }

    const auto envelope = api.get(buildUrl(kPgcPlayUrlEndpoint, params), cookie);
    // PGC 信封把载荷嵌在 result 而非 data。
    const QJsonValue resultValue = envelope.root.value("result");
    if (!resultValue.isObject()) {
        throw ApiError(0, Loc::get("播放地址接口返回缺少 data。"), 0);
    }
    return resultValue.toObject();
}

PlayerApi::PgcEntitlement PlayerApi::parsePgcEntitlement(const QJsonObject &result) {
    PgcEntitlement out;
    // is_preview / has_paid 在 PGC 端点是 JSON 布尔(主站形态是 1/0 数字 ——
    // jh::getBool 两种形态都读,与 C# GetBoolean 的 true|非零 数字 口径一致)
    out.isPreview = jh::getBool(result, "is_preview");
    out.hasPaid = jh::getBool(result, "has_paid");
    out.vipType = jh::getInt64(result, "vip_type");
    return out;
}

// ---- 共用档位/编码选择 ----

PlayerApi::DashPlayInfo PlayerApi::selectDash(const QJsonObject &data, const QJsonObject &dash,
                                              int preferQn, const QString &preferCodec) {
    QList<DashFormat> formats = parseDashFormats(data);
    const QList<DashSegment> videos = parseDashSegments(dash, "video");
    const QList<DashSegment> audios = parseDashSegments(dash, "audio");
    if (videos.isEmpty()) {
        throw ApiError(0, Loc::get("响应中没有可用的 DASH 视频段。"), 0);
    }

    // 档位目录与实际视频段交叉校验:目录(support_formats / accept_quality)会列出
    // 账号实际拿不到的档位 —— 反之,下发的视频段即使带 VIP 标记也照样可播(免费
    // PGC 剧集会向普通账号下发全档 dash)。段的存在与否才是权益真相;仅存在于目录
    // 的档位在菜单里渲染为禁用。
    QSet<int> segmentQualities;
    for (const DashSegment &segment : videos) {
        segmentQualities.insert(segment.quality);
    }
    for (DashFormat &format : formats) {
        format.available = segmentQualities.contains(format.quality);
    }

    // 防御性补全:实际下发但目录缺失的档位也要有菜单行(含选中指示)。C# 原文
    // 注释写以「{qn}P」标签兜底,代码实际传空标签 —— 原样保留,由消费方回退。
    for (const int quality : segmentQualities) {
        bool known = false;
        for (const DashFormat &format : formats) {
            if (format.quality == quality) {
                known = true;
                break;
            }
        }
        if (!known) {
            DashFormat synthesized;
            synthesized.quality = quality;
            synthesized.label = QString();
            synthesized.needVip = false;
            synthesized.available = true;
            formats.append(synthesized);
        }
    }

    DashPlayInfo out;
    out.formats = formats;

    // 候选链:每个视频段/音频段一条(主 + backup,稳定 CDN 在前)。C# 只回传所选
    // 段的链;此处按要求保留全部段的链。
    for (const DashSegment &segment : videos) {
        out.videoCandidates.append(orderByStableCdn(segment.baseUrl, segment.backupUrls));
        out.videoQualities.append(segment.quality);
        out.videoCodecs.append(segment.codec);
    }
    for (const DashSegment &segment : audios) {
        out.audioCandidates.append(orderByStableCdn(segment.baseUrl, segment.backupUrls));
    }
    out.hasAudio = !out.audioCandidates.isEmpty();

    // 优先请求的档位(回退到账号实际拿到的最高档),档位内先匹配偏好编码
    // (avc/hev/acd…),再任意编码;音频取最高规格段。
    bool hasPreferred = false;
    int maxQuality = videos.first().quality;
    for (const DashSegment &segment : videos) {
        if (segment.quality == preferQn) hasPreferred = true;
        if (segment.quality > maxQuality) maxQuality = segment.quality;
    }
    const int targetQuality = hasPreferred ? preferQn : maxQuality;

    const DashSegment *chosen = nullptr;
    for (const DashSegment &segment : videos) {
        if (segment.quality == targetQuality && segment.codec.contains(preferCodec)) {
            chosen = &segment;
            break;
        }
    }
    if (chosen == nullptr) {
        for (const DashSegment &segment : videos) {
            if (segment.quality == targetQuality) {
                chosen = &segment;
                break;
            }
        }
    }

    out.selectedQn = chosen->quality;
    out.selectedCodec = chosen->codec;
    out.selectedVideoUrls = orderByStableCdn(chosen->baseUrl, chosen->backupUrls);

    const DashSegment *audio = nullptr;
    for (const DashSegment &segment : audios) {
        // OrderByDescending 稳定排序取首:同规格保持原始相对次序
        if (audio == nullptr || segment.quality > audio->quality) audio = &segment;
    }
    if (audio != nullptr) {
        out.selectedAudioUrls = orderByStableCdn(audio->baseUrl, audio->backupUrls);
    }
    return out;
}

QStringList PlayerApi::orderByStableCdn(const QString &baseUrl, const QStringList &backupUrls) {
    QStringList all{baseUrl};
    all.append(backupUrls);
    return MediaUrlPolicy::ordered(all);
}

QList<PlayerApi::DashSegment> PlayerApi::parseDashSegments(const QJsonObject &dash,
                                                           const QString &property) {
    QList<DashSegment> result;
    const QJsonArray segments = jh::getChildArray(dash, property);
    for (const QJsonValue &value : segments) {
        if (!value.isObject()) continue;
        const QJsonObject segment = value.toObject();

        QString baseUrl = jh::getString(segment, "baseUrl");
        if (baseUrl.trimmed().isEmpty()) {
            baseUrl = jh::getString(segment, "base_url");
        }
        if (baseUrl.trimmed().isEmpty()) {
            baseUrl = jh::getString(segment, "BaseUrl");
        }

        DashSegment out;
        // 字段变体均保留：空 camel 数组不能遮蔽有效 snake 备选；排序时去重。
        for (const char *key : {"backupUrl", "backup_url"}) {
            for (const QJsonValue &backup : segment.value(key).toArray()) {
                const QString url = backup.toString();
                if (!url.isEmpty()) out.backupUrls.append(url);
            }
        }

        // 某些响应仅保留备用地址，仍可作为候选流；空链不能进入选流。
        if (orderByStableCdn(baseUrl, out.backupUrls).isEmpty()) continue;
        out.quality = static_cast<int>(jh::getInt64(segment, "id"));
        out.codec = jh::getString(segment, "codecs");
        out.baseUrl = baseUrl;
        result.append(out);
    }
    return result;
}

QList<PlayerApi::DashFormat> PlayerApi::parseDashFormats(const QJsonObject &data) {
    QList<DashFormat> result;
    // support_formats 是权威清单(标签 + need_vip);accept_quality +
    // accept_description 是它缺席时的回退。need_vip 在 PGC 端点是 JSON 布尔、
    // 主站是 1/0 —— 两种形态都读。
    const QJsonArray supported = jh::getChildArray(data, "support_formats");
    for (const QJsonValue &value : supported) {
        if (!value.isObject()) continue;
        const QJsonObject format = value.toObject();
        const int quality = static_cast<int>(jh::getInt64(format, "quality"));
        if (quality > 0) {
            DashFormat out;
            out.quality = quality;
            out.label = jh::getString(format, "new_description");
            out.needVip = jh::getBool(format, "need_vip");
            out.available = true;
            result.append(out);
        }
    }
    if (!result.isEmpty()) return result;

    const QJsonArray qualities = jh::getChildArray(data, "accept_quality");
    const QJsonArray descriptions = jh::getChildArray(data, "accept_description");
    for (int i = 0; i < qualities.size(); ++i) {
        const QJsonValue element = qualities.at(i);
        int quality = 0;
        if (element.isDouble()) {
            quality = static_cast<int>(element.toDouble());
        } else if (element.isString()) {
            quality = element.toString().toInt();
        }
        if (quality > 0) {
            DashFormat out;
            out.quality = quality;
            out.label = i < descriptions.size() ? descriptions.at(i).toString() : QString();
            out.needVip = false;
            out.available = true;
            result.append(out);
        }
    }
    return result;
}

// ---- VIP 态 ----

bool PlayerApi::getVipStatus(BilibiliApiClient &api, const QString &cookie) {
    try {
        const auto envelope = api.get(QUrl(kNavEndpoint), cookie);
        const QJsonObject data = jh::getChild(envelope.root, "data");
        return jh::getInt64(data, "vipStatus") == 1;
    } catch (const ApiError &ex) {
        // C#:信封 code 非 0(含未登录的 -101)一律 false;传输层失败照抛
        if (ex.code() != 0) return false;
        throw;
    }
}

// ---- 字幕 ----

QList<PlayerApi::SubtitleTrack> PlayerApi::getSubtitleTracks(BilibiliApiClient &api, qint64 aid,
                                                             qint64 cid, const QString &cookie,
                                                             qint64 epId) {
    // PGC playlist entries often carry only ep_id/cid. Resolve the archive ID
    // before calling the web player information endpoint (aid/bvid required).
    if (epId > 0 && aid <= 0) {
        const auto season = api.get(buildUrl(kPgcSeasonEndpoint,
                {{"ep_id", QString::number(epId)}}), cookie);
        const auto episodes = jh::getChildArray(jh::getChild(season.root, "result"), "episodes");
        for (const auto &value : episodes) {
            const auto episode = value.toObject();
            if (jh::getInt64(episode, "id") != epId && jh::getInt64(episode, "ep_id") != epId) continue;
            aid = jh::getInt64(episode, "aid");
            if (cid <= 0) cid = jh::getInt64(episode, "cid");
            break;
        }
    }
    if (aid <= 0 || cid <= 0) return {};
    QMap<QString, QString> params;
    params.insert("aid", QString::number(aid));
    params.insert("cid", QString::number(cid));
    if (epId > 0) params.insert("ep_id", QString::number(epId));
    appendWbiParams(&params, api, cookie);

    const auto envelope = api.get(buildUrl(kPlayerInfoEndpoint, params), cookie);
    return parseSubtitleTracks(jh::getChild(envelope.root, "data"));
}

QList<PlayerApi::SubtitleTrack> PlayerApi::parseSubtitleTracks(const QJsonObject &data) {
    QList<SubtitleTrack> tracks;
    const QJsonArray subtitles = jh::getChildArray(
            jh::getChild(data, "subtitle"), "subtitles");
    for (const QJsonValue &value : subtitles) {
        if (!value.isObject()) continue;
        const QJsonObject element = value.toObject();
        const QString lan = jh::getString(element, "lan");
        QString url = jh::getString(element, "subtitle_url");
        if (url.trimmed().isEmpty()) continue;
        if (url.startsWith("//")) {
            url = "https:" + url;
        }
        if (!MediaUrlPolicy::valid(url)) continue;

        SubtitleTrack track;
        track.lan = lan;
        track.lanDoc = jh::getString(element, "lan_doc");
        track.url = url;
        // AI 生成轨的 lan 以 "ai" 开头(docs/video/player.md)
        track.isAi = lan.startsWith("ai", Qt::CaseInsensitive)
                || track.lanDoc.contains("AI", Qt::CaseInsensitive)
                || jh::getInt64(element, "ai_type") > 0;
        tracks.append(track);
    }
    return tracks;
}

QString PlayerApi::getSubtitleJson(BilibiliApiClient &api, const QString &url,
                                   const QString &cookie) {
    // 非信封载荷:纯文本 GET,仅校验 HTTP 状态(getBytes 内部完成;Cookie/Referer
    // 头由统一管线注入)
    return QString::fromUtf8(api.getBytes(QUrl(url), cookie));
}

// ---- 弹幕存档 ----

QString PlayerApi::getDanmakuXml(BilibiliApiClient &api, qint64 cid, const QString &cookie) {
    const QUrl url(QString::fromLatin1(kDanmakuEndpoint) + QString::number(cid) +
                   QStringLiteral(".xml"));
    const QByteArray bytes = api.getBytes(url, cookie);
    return decompressDanmaku(bytes);
}

QString PlayerApi::decompressDanmaku(const QByteArray &bytes) {
    if (bytes.isEmpty()) return QString();

    QByteArray out;
    // gzip: 1f 8b —— zlib deflate 以 78 01/9c/da 开头
    if (bytes.size() >= 2 && (bytes.at(0) & 0xFF) == 0x1F && (bytes.at(1) & 0xFF) == 0x8B) {
        if (!inflateAll(bytes, 16 + MAX_WBITS, &out)) {
            throw ApiError(0, Loc::get("弹幕数据解压失败"), 0);
        }
    } else if ((bytes.at(0) & 0xFF) == 0x78) {
        // zlib 壳:跳过 2 字节头按 raw deflate 解(忽略尾部 adler32;C#
        // DeflateStream(bytes, 2, len - 2) 同)
        if (!inflateAll(bytes.mid(2), -MAX_WBITS, &out)) {
            throw ApiError(0, Loc::get("弹幕数据解压失败"), 0);
        }
    } else if (!inflateAll(bytes, -MAX_WBITS, &out)) {
        // raw deflate 也解不开:按原样当 UTF-8 文本(未压缩存档)
        return QString::fromUtf8(bytes);
    }
    return QString::fromUtf8(out);
}
