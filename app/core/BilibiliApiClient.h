#ifndef BILIBILI_API_CLIENT_H
#define BILIBILI_API_CLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QNetworkProxy>
#include <QMap>
#include <QUrl>

class QThread;
class QNetworkAccessManager;

// B 站 web API 统一请求管线(契约见 openspec/specs/bilibili-api-client):
// 单一 QNetworkAccessManager + 统一浏览器头(UA/Referer/Origin + 每 Cookie)+ 信封校验
// (code=0 成功;-101 → ApiUnauthorizedError;其余非零 → ApiError(code/message);
// HTTP 非 2xx → ApiError(httpStatus))。
//
// 线程模型:实例常驻专用工作线程,公开阻塞方法可在任意线程调用(内部阻塞投递)。
// 新端点接入:先查 b3/bilibili-api-collect/docs/<域>,再以域模块挂本管线,禁止散装 HTTP。
class BilibiliApiClient : public QObject {
    Q_OBJECT
   public:
    struct Envelope {
        QByteArray raw;
        QJsonObject root;
    };

    static BilibiliApiClient *instance();
    // Immutable request scope, constructed and used on the same worker thread.
    // Only regional PGC callers opt in; no application/system proxy mutation.
    explicit BilibiliApiClient(const QNetworkProxy &proxy);
    ~BilibiliApiClient() override;

    BilibiliApiClient(const BilibiliApiClient &) = delete;
    BilibiliApiClient &operator=(const BilibiliApiClient &) = delete;

    // GET + 信封校验
    Envelope get(const QUrl &url, const QString &cookie);
    // POST application/x-www-form-urlencoded + 信封校验(无 data 体视为成功)
    Envelope postForm(const QUrl &url, const QMap<QString, QString> &form,
                      const QString &cookie);
    // GET 原始字节(非信封载荷,如弹幕 XML);仍校验 HTTP 状态
    QByteArray getBytes(const QUrl &url, const QString &cookie);

    // cookie 文件读取与三预检(缺失/空/无 SESSDATA),文案与原项目一致
    static QString readCookieFromFile(const QString &cookiePath);
    // 信封校验并返回根对象(裸校验,供已取回文本的调用方复用)
    static QJsonObject checkEnvelope(const QByteArray &json);

    static QString userAgent() { return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"); }
    static QString referer() { return QStringLiteral("https://www.bilibili.com/"); }
    static QString origin() { return QStringLiteral("https://www.bilibili.com"); }

   private:
    explicit BilibiliApiClient();

    Envelope getInternal(const QUrl &url, const QString &cookie);
    Envelope postFormInternal(const QUrl &url, const QMap<QString, QString> &form,
                              const QString &cookie);
    QByteArray getBytesInternal(const QUrl &url, const QString &cookie);

    QNetworkAccessManager *networkManager();
    QNetworkProxy proxy_{QNetworkProxy::NoProxy};
    QThread *thread_ = nullptr;
    class QNetworkAccessManager *nam_ = nullptr;  // 惰性创建于工作线程
};

#endif  // BILIBILI_API_CLIENT_H
