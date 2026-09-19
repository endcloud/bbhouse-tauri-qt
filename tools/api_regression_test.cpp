// 无网络、无 Cookie：使用文档样例校验签名与播放响应兼容性。
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

#include "core/ApiErrors.h"
#include "core/CardAuthor.h"
#include "core/BilibiliApiClient.h"
#include "core/JsonHelpers.h"
#include "core/PlayerApi.h"
#include "core/MediaUrlPolicy.h"
#include "player/SubtitleTimeline.h"
#include "core/WbiSigner.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    const auto subtitles = PlayerApi::parseSubtitleTracks(QJsonDocument::fromJson(R"JSON({
      "subtitle":{"subtitles":[
        {"lan":"zh-CN","lan_doc":"中文","subtitle_url":"//subtitle.example.test/manual.json"},
        {"lan":"ai-zh","lan_doc":"中文（自动生成）","subtitle_url":"https://subtitle.example.test/ai.json"},
        {"lan":"en","lan_doc":"English AI","subtitle_url":"https://subtitle.example.test/en.json"},
        {"lan":"zh","ai_type":1,"subtitle_url":"https://subtitle.example.test/type.json"},
        {"lan":"bad","subtitle_url":"file:///etc/passwd"},
        {"lan":"empty","subtitle_url":""}]}})JSON").object());
    check(subtitles.size() == 4 && !subtitles[0].isAi && subtitles[1].isAi
          && subtitles[2].isAi && subtitles[3].isAi && subtitles[0].url.startsWith("https://"),
          "字幕保留人工/AI语言前缀/AI名称/ai_type轨并排除非HTTP资源");
    const auto timeline = SubtitleTimeline::fromJson(R"JSON({"body":[
      {"from":3,"to":4,"content":"second"}, {"from":1,"to":5,"content":"long"},
      {"from":1,"to":2,"content":"first"}, {"from":-1,"to":3,"content":"invalid"},
      {"from":2,"to":2,"content":"zero"}, {"from":4,"to":5,"content":""}]})JSON");
    check(timeline.textAt(0).isEmpty() && timeline.textAt(1) == "long\nfirst"
          && timeline.textAt(2) == "long" && timeline.textAt(3) == "long\nsecond"
          && timeline.textAt(5).isEmpty() && timeline.textAt(1.5) == "long\nfirst",
          "字幕乱序/重叠/半开区间/向前向后跳转与无效cue过滤");
    try { SubtitleTimeline::fromJson("{broken"); check(false, "损坏字幕JSON被拒绝"); }
    catch (const std::exception &) { check(true, "损坏字幕JSON被拒绝"); }
    const QString mixin = QStringLiteral("ea1db124af3c7062474693fa704f4ff8");
    const QMap<QString, QString> params{{"foo", "114"}, {"bar", "514"}, {"zab", "1919810"}};
    const auto signedPair = WbiSigner::signWithMixinKey(params, mixin, 1702204169);
    check(signedPair.first == "8f6f2b5b3d485fe1886cec6a0be8c5d4" &&
          signedPair.second == "1702204169", "WBI 文档固定向量");
    check(WbiSigner::signWithMixinKey({{"foo", "a!b'c(d)* 中文"}}, mixin, 1702204169) ==
          WbiSigner::signWithMixinKey({{"foo", "abcd 中文"}}, mixin, 1702204169),
          "WBI 特殊字符过滤");
    try {
        WbiSigner::signWithMixinKey(params, "short", 1702204169);
        check(false, "WBI 畸形密钥拒绝");
    } catch (const ApiError &) { check(true, "WBI 畸形密钥拒绝"); }

    const auto payload = QJsonDocument::fromJson(R"JSON({
      "support_formats":[{"quality":80,"new_description":"1080P"},{"quality":120,"need_vip":1}],
      "dash":{
        "video":[{"id":80,"codecs":"avc1.640028","base_url":"https://peer.mcdn.bilivideo.cn/video",
          "backup_url":["https://cdn.bilivideo.com/video","https://cdn.bilivideo.com/video"]}],
        "audio":[{"id":30280,"codecs":"mp4a.40.2","baseUrl":"https://cdn.bilivideo.com/audio"}]
      }
    })JSON").object();
    const auto dash = PlayerApi::selectDash(payload, payload.value("dash").toObject(), 80, "avc");
    check(dash.selectedQn == 80 && dash.hasAudio && dash.selectedVideoUrls.size() == 2,
          "DASH base_url + backup_url 与音轨解析");
    check(dash.selectedVideoUrls.first() == "https://cdn.bilivideo.com/video",
          "稳定 CDN 优先并去重");
    check(!dash.formats.at(1).available && dash.formats.at(1).needVip,
          "未下发清晰度不可选择");
    const QJsonObject backupOnly{{"video", QJsonArray{QJsonObject{
        {"id", 64}, {"backupUrl", QJsonArray{"https://cdn.bilivideo.com/backup"}}}}}};
    check(PlayerApi::selectDash({}, backupOnly, 64, "avc").selectedVideoUrls.size() == 1,
          "仅备用 URL 的 DASH 流可播");
    try {
        PlayerApi::selectDash({}, QJsonObject{{"video", QJsonArray{QJsonObject{
            {"id", 80}, {"baseUrl", "invalid-url"}}}}}, 80, "avc");
        check(false, "拒绝空或非法 DASH 地址");
    } catch (const ApiError &) { check(true, "拒绝空或非法 DASH 地址"); }
    const auto mp4 = PlayerApi::parseDurlFromData(QJsonObject{
        {"quality", 64}, {"durl", QJsonArray{QJsonObject{
            {"backup_url", QJsonArray{"", "https://cdn.bilivideo.com/video.mp4"}}}}}});
    check(mp4.url.endsWith("video.mp4"), "MP4 backup_url 为数组");
    // 全部为虚构无签名地址；主备链的顺序直接决定实际加载顺序。
    const QString cloud = "https://upos-sz-mirrorcos.bilivideo.com/media?keep=1%2F2&x=a,b";
    const QString cloud2 = "https://upos-sz-estgoss.bilivideo.com/media";
    const QString ordinary = "https://cn-fixture.bilivideo.com/media?hint=mcdn";
    const QString mcdn = "https://peer.mcdn.bilivideo.cn:4483/media";
    const QString pcdn = "https://peer.PCDN.example.test/media";
    const QString szbdyd = "https://edge.szbdyd.com/media";
    const QStringList mixed{mcdn, ordinary, pcdn, cloud, szbdyd, cloud2, cloud};
    const QStringList ordered{cloud, cloud2, ordinary, mcdn, pcdn, szbdyd};
    check(MediaUrlPolicy::ordered(mixed) == ordered,
          "公有云先于普通节点，mcdn/PCDN/szbdyd 殿后，同级保序并保留 URL 原文");
    check(MediaUrlPolicy::priority("https://upos-sz-mirrorhw.bilivideo.com/media") == 0
          && MediaUrlPolicy::priority("https://upos-sz-mirrorkodo.bilivideo.com/media") == 0
          && MediaUrlPolicy::priority("https://upos-hz-mirrorakam.akamaized.net/media") == 0
          && MediaUrlPolicy::priority("https://upos-sz-mirrorcos.bilivideo.com.example.test/media") == 1
          && MediaUrlPolicy::priority("https://cdn.example.test:4483/media") == 1,
          "云 CDN 主机边界明确，未知主机或非标准端口不误判");
    check(MediaUrlPolicy::ordered({mcdn, pcdn, mcdn}) == QStringList{mcdn, pcdn},
          "只有 P2P 仍保留可用备选");
    check(MediaUrlPolicy::ordered({"", "garbage", "file:///video", "https://user:secret@example.test/a",
          "https://cdn.example.test/a#part", "https://cdn.example.test/a\nHeader:x", cloud}) == QStringList{cloud},
          "非法媒体 URL 不进入候选，不损坏有效 URL");
    QJsonArray backups;
    for (const QString &url : mixed.mid(1)) backups.append(url);
    const QJsonObject mixedSegment{{"id", 80}, {"codecs", "avc1"}, {"baseUrl", mcdn},
                                  {"backupUrl", QJsonArray{}}, {"backup_url", backups}};
    QJsonObject mixedAudio = mixedSegment;
    mixedAudio.insert("id", 30280);
    mixedAudio.insert("codecs", "mp4a");
    const auto mixedDash = PlayerApi::selectDash({}, {{"video", QJsonArray{mixedSegment}},
                                                     {"audio", QJsonArray{mixedAudio}}}, 80, "avc");
    check(mixedDash.selectedVideoUrls == ordered && mixedDash.selectedAudioUrls == ordered
          && mixedDash.videoCandidates.first() == ordered && mixedDash.audioCandidates.first() == ordered,
          "UGC/PGC 共用 DASH 视频音频完整主备排序，空 camel 字段不遮蔽 snake 字段");
    const auto mixedMp4 = PlayerApi::parseDurlFromData({{"quality", 80}, {"durl", QJsonArray{
        QJsonObject{{"url", mcdn}, {"backup_url", backups}}}}});
    check(mixedMp4.url == cloud && mixedMp4.urls == ordered,
          "UGC/PGC durl 从主备链优选云 CDN 并保留全部回退地址");
    const auto invalidBase = PlayerApi::parseDurlFromData({{"durl", QJsonArray{QJsonObject{
        {"url", "not-a-url"}, {"backupUrl", QJsonArray{pcdn, cloud}}}}}});
    check(invalidBase.urls == QStringList{cloud, pcdn}, "durl 无效主地址使用 camel 备选");
    try {
        PlayerApi::parseDurlFromData({{"durl", QJsonArray{QJsonObject{{"url", "file:///bad"}}}}});
        check(false, "durl 全非法候选明确失败");
    } catch (const ApiError &) { check(true, "durl 全非法候选明确失败"); }
    const auto freePgc = PlayerApi::parsePgcEntitlement(
            QJsonObject{{"is_preview", 0}, {"has_paid", false}});
    const auto previewPgc = PlayerApi::parsePgcEntitlement(
            QJsonObject{{"is_preview", true}, {"has_paid", 0}});
    check(!freePgc.isPreview && !freePgc.hasPaid && previewPgc.isPreview,
          "PGC 免费与试看权益原值区分");
    const QJsonObject season{{"episodes", QJsonArray{
        QJsonObject{{"id", 12345}, {"cid", 2147483648LL}},
        QJsonObject{{"ep_id", 12346}, {"cid", 2147483649LL}}}}};
    check(PlayerApi::parsePgcEpisodeCid(season, 12345) == 2147483648LL &&
          PlayerApi::parsePgcEpisodeCid(season, 12346) == 2147483649LL &&
          PlayerApi::parsePgcEpisodeCid(season, 0) == 0,
          "PGC id/ep_id 与 64 位 cid");
    check(jh::getInt64(QJsonDocument::fromJson("{\"aid\":9007199254740993}").object(), "aid") ==
          9007199254740993LL, "JSON 64 位 ID 不经 double 舍入");
    HistoryItem authorItem;
    authorItem.rawJson = R"({"author_face":"//i0.hdslb.com/author.jpg"})";
    check(historyAuthorFaceUrl(authorItem) == "https://i0.hdslb.com/author.jpg",
          "已有历史原始载荷恢复作者头像，无需数据库迁移");
    authorItem.rawJson = R"({"owner":{"face":"//i0.hdslb.com/owner.jpg"}})";
    check(historyAuthorFaceUrl(authorItem) == "https://i0.hdslb.com/owner.jpg",
          "稍后再看 owner 头像归一化");
    authorItem.rawJson = "invalid json";
    check(historyAuthorFaceUrl(authorItem).isEmpty(), "缺失或异常作者头像保留空值供默认图兜底");
    try {
        BilibiliApiClient::checkEnvelope("{\"code\":-101,\"message\":\"not logged in\"}");
        check(false, "登录失效类型映射");
    } catch (const ApiUnauthorizedError &) { check(true, "登录失效类型映射"); }
    try {
        BilibiliApiClient::checkEnvelope("{\"code\":-404,\"message\":\"not found\"}");
        check(false, "API -404 与 HTTP 404 区分");
    } catch (const ApiError &e) {
        check(e.code() == -404 && e.httpStatus() == 0, "API -404 与 HTTP 404 区分");
    }
    const ApiError privateError(403, QStringLiteral(
        "request https://fixture-user:fixture-password@example.invalid/video?sign=fixture-signature failed; "
        "SESSDATA=fixture-session; bili_jct=fixture-csrf\nCookie: secret-other=value\n"
        "Authorization: Bearer fixture-bearer"), 403);
    const QString diagnostic = privateError.message();
    check(!diagnostic.contains("fixture-") && !diagnostic.contains("secret-other") &&
          !QString::fromUtf8(privateError.what()).contains("fixture-") &&
          privateError.httpStatus() == 403 && privateError.code() == 403,
          "API 异常脱敏 URL、Cookie 和鉴权字段后才允许显示或入库");
    check(privateDiagnostic(QStringLiteral("普通网络错误 code=-404")) == "普通网络错误 code=-404",
          "脱敏保留无隐私的诊断文本");
    check(!privateDiagnostic(QStringLiteral(
              R"({"SESSDATA":"fixture-json", "bili_jct":"fixture-csrf"})"))
              .contains("fixture-") &&
          !privateDiagnostic(QStringLiteral(R"({"Authorization":"Bearer fixture-auth"})"))
              .contains("fixture-"),
          "服务端回显 JSON 形式的凭据也会脱敏");
    std::printf("API_REGRESSION_%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
