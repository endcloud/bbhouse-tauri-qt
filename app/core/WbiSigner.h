#ifndef WBI_SIGNER_H
#define WBI_SIGNER_H

#include <QMap>
#include <QString>
#include <QMutex>

class BilibiliApiClient;

// WBI 签名(w_rid/wts),口径见 b3/bilibili-api-collect docs/misc/sign/wbi.md:
// nav 端点取 img/sub key → MIXIN_KEY_ENC_TAB 置换取前 32 位 → 参数加 wts、
// 按键升序、移除值中的 !'()* 并百分号编码拼接 → w_rid = md5(query + mixin_key)。
// 键站点级、每日轮换,进程内缓存 4 小时。
class WbiSigner {
   public:
    // 返回 (w_rid, wts);parameters 不含 wts/w_rid
    static QPair<QString, QString> sign(BilibiliApiClient &api, const QString &cookie,
                                        QMap<QString, QString> parameters);

    // 纯签名算法：已派生的 32 字符 mixin key + 固定时间可用于离线验证。
    static QPair<QString, QString> signWithMixinKey(QMap<QString, QString> parameters,
                                                   const QString &mixinKey, qint64 timestamp);

   private:
    static QString getMixinKey(BilibiliApiClient &api, const QString &cookie);
    static QString extractKey(const QString &url);
    static QString encodeValue(const QString &value);

    static QMutex cacheLock_;
    static QString cachedMixinKey_;
    static qint64 cachedAtMs_;  // epoch ms
};

#endif  // WBI_SIGNER_H
