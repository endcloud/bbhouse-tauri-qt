#include "core/HeartbeatApi.h"

#include <QDateTime>
#include <QMap>
#include <algorithm>
#include <cmath>

#include "core/CookieHelpers.h"

namespace {
constexpr const char *kEndpoint = "https://api.bilibili.com/x/click-interface/web/heartbeat";
}  // namespace

void HeartbeatApi::report(BilibiliApiClient &api, const QString &cookie,
                          const HeartbeatReport &reportData) {
    report(api, cookie, reportData, QUrl(QString::fromUtf8(kEndpoint)));
}

void HeartbeatApi::report(BilibiliApiClient &api, const QString &cookie,
                          const HeartbeatReport &reportData, const QUrl &endpoint) {
    const bool isPgc = reportData.epId > 0 && reportData.aid <= 0;
    // 秒数四舍五入并截非负(对齐 C# Math.Round 默认的银行家舍入:
    // std::nearbyint 走默认舍入模式 FE_TONEAREST)
    const double roundedSeconds = std::max(0.0, std::nearbyint(reportData.playedSeconds));
    const QString playedSecondsText = QString::number(static_cast<qint64>(roundedSeconds));

    QMap<QString, QString> form;
    // 投机字段的同值写法,依文档自身建议
    form.insert("played_time", playedSecondsText);
    form.insert("realtime", playedSecondsText);
    form.insert("start_ts", QString::number(QDateTime::currentSecsSinceEpoch()));
    form.insert("type", isPgc ? QStringLiteral("4") : QStringLiteral("3"));
    form.insert("dt", QStringLiteral("2"));
    form.insert("play_type", QString::number(static_cast<int>(reportData.action)));

    if (reportData.cid > 0) {
        form.insert("cid", QString::number(reportData.cid));
    }

    if (isPgc) {
        form.insert("epid", QString::number(reportData.epId));
    } else {
        form.insert("aid", QString::number(reportData.aid));
    }

    const QString csrf = tryGetBiliJct(cookie);
    if (!csrf.isEmpty()) {
        form.insert("csrf", csrf);
    }

    api.postForm(endpoint, form, cookie);
}

QString HeartbeatApi::tryGetBiliJct(const QString &cookie) {
    return CookieHelpers::tryGetCookieValue(cookie, QStringLiteral("bili_jct"));
}
