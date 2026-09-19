#include "core/WbiSigner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QUrl>
#include <algorithm>

#include "core/BilibiliApiClient.h"
#include "core/ApiErrors.h"
#include "core/JsonHelpers.h"

QMutex WbiSigner::cacheLock_;
QString WbiSigner::cachedMixinKey_;
qint64 WbiSigner::cachedAtMs_ = 0;

namespace {
constexpr qint64 kKeyLifetimeMs = 4LL * 3600 * 1000;

// wbi.md 固定置换表——对 (img_key + sub_key) 重排后截断
const int kMixinKeyEncTab[64] = {
        46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
        27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
        37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
        22, 25, 54, 21, 56, 59, 6,  63, 57, 62, 11, 36, 20, 34, 44, 52};

constexpr const char *kNavEndpoint = "https://api.bilibili.com/x/web-interface/nav";
}  // namespace

QPair<QString, QString> WbiSigner::sign(BilibiliApiClient &api, const QString &cookie,
                                        QMap<QString, QString> parameters) {
    return signWithMixinKey(parameters, getMixinKey(api, cookie),
                            QDateTime::currentSecsSinceEpoch());
}

QPair<QString, QString> WbiSigner::signWithMixinKey(QMap<QString, QString> parameters,
                                                  const QString &mixinKey, qint64 timestamp) {
    if (mixinKey.size() != 32) throw ApiError(0, Loc::get("WBI 密钥获取失败"), 0);
    const QString wts = QString::number(timestamp);
    // 文档要求移除 !'()* 后再做 UTF-8 百分号编码，空格使用 %20。
    parameters.remove("w_rid");
    parameters.insert("wts", wts);

    QStringList parts;
    QList<QString> keys = parameters.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) {
        parts << QString::fromLatin1(QUrl::toPercentEncoding(key)) + "=" +
                         encodeValue(parameters.value(key));
    }
    const QString query = parts.join("&");

    const QByteArray hash =
            QCryptographicHash::hash((query + mixinKey).toUtf8(),
                                     QCryptographicHash::Md5)
                    .toHex();
    return {QString::fromLatin1(hash).toLower(), wts};
}

QString WbiSigner::getMixinKey(BilibiliApiClient &api, const QString &cookie) {
    {
        QMutexLocker locker(&cacheLock_);
        if (!cachedMixinKey_.isEmpty() &&
            QDateTime::currentMSecsSinceEpoch() - cachedAtMs_ < kKeyLifetimeMs) {
            return cachedMixinKey_;
        }
    }

    // 统一管线信封校验已映射 -101/非零 code,此处仅处理空键
    const auto envelope = api.get(QUrl(kNavEndpoint), cookie);
    const QJsonObject wbi =
            jh::getChild(jh::getChild(envelope.root, "data"), "wbi_img");
    const QString imgKey = extractKey(jh::getString(wbi, "img_url"));
    const QString subKey = extractKey(jh::getString(wbi, "sub_url"));
    if (imgKey.size() != 32 || subKey.size() != 32) {
        throw ApiError(0, Loc::get("WBI 密钥获取失败"), 0);
    }

    const QString raw = imgKey + subKey;
    QString mixin;
    for (int i = 0; i < 32; ++i) mixin += raw[kMixinKeyEncTab[i]];

    {
        QMutexLocker locker(&cacheLock_);
        cachedMixinKey_ = mixin;
        cachedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    }
    return mixin;
}

QString WbiSigner::extractKey(const QString &url) {
    if (url.trimmed().isEmpty()) return QString();
    const int slash = url.lastIndexOf('/');
    QString fileName = slash >= 0 ? url.mid(slash + 1) : url;
    const int dot = fileName.lastIndexOf('.');
    if (dot > 0) fileName = fileName.left(dot);
    return fileName;
}

QString WbiSigner::encodeValue(const QString &value) {
    QString filtered = value;
    for (const QChar character : QStringLiteral("!'()*")) filtered.remove(character);
    return QString::fromLatin1(QUrl::toPercentEncoding(filtered));
}
