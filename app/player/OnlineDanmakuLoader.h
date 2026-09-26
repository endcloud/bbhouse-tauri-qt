#ifndef ONLINE_DANMAKU_LOADER_H
#define ONLINE_DANMAKU_LOADER_H

#include <QCryptographicHash>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <functional>
#include "core/BilibiliApiClient.h"
#include "core/PlayerApi.h"
#include "core/DanmakuParser.h"

// One current content request, independent of the video load/quality generation.
// The loader keeps no second copy of the parsed list after delivery.
class OnlineDanmakuLoader final : public QObject {
public:
    using Fetch = std::function<QString(qint64, const QString &)>;
    explicit OnlineDanmakuLoader(QObject *parent = nullptr, Fetch fetch = {})
        : QObject(parent), fetch_(fetch ? std::move(fetch) : Fetch([](qint64 cid, const QString &cookie) {
              return PlayerApi::getDanmakuXml(*BilibiliApiClient::instance(), cid, cookie);
          })) {
        pool_.setMaxThreadCount(1);
    }
    ~OnlineDanmakuLoader() override {
        reset();
        pool_.waitForDone();
    }

    std::function<void(QVariantList)> loaded;
    std::function<void(QString)> failed;

    void reset() {
        ++epoch_;
        cid_ = 0;
        cookieDigest_.clear();
        pool_.clear();
    }

    void request(qint64 cid, const QString &cookie) {
        if (cid <= 0) return;
        const auto digest = QCryptographicHash::hash(cookie.toUtf8(), QCryptographicHash::Sha256);
        if (cid == cid_ && digest == cookieDigest_) return; // Pending or already delivered.
        reset();
        cid_ = cid;
        cookieDigest_ = digest;
        const quint64 epoch = epoch_;
        const Fetch fetch = fetch_;
        pool_.start([this, fetch, cid, cookie, epoch] {
            QVariantList entries;
            QString error;
            bool succeeded = false;
            try {
                // Destroy raw XML and parsed structs before queuing the UI list.
                const auto parsed = DanmakuParser::parseXml(fetch(cid, cookie));
                entries.reserve(parsed.size());
                for (const auto &entry : parsed) {
                    entries.append(QVariantMap{{"time", entry.time}, {"type", entry.type},
                        {"fontSize", entry.fontSize}, {"fontColor", entry.fontColor},
                        {"level", entry.level}, {"message", entry.message}});
                }
                succeeded = true;
            } catch (const std::exception &exception) {
                error = QString::fromUtf8(exception.what());
            }
            QMetaObject::invokeMethod(this, [this, epoch, succeeded, entries = std::move(entries), error] {
                if (epoch != epoch_) return;
                if (succeeded) {
                    if (loaded) loaded(entries);
                } else {
                    // A later quality selection may retry a failed optional load.
                    cid_ = 0;
                    cookieDigest_.clear();
                    if (failed) failed(error);
                }
            }, Qt::QueuedConnection);
        });
    }

private:
    QThreadPool pool_;
    Fetch fetch_;
    quint64 epoch_ = 0;
    qint64 cid_ = 0;
    QByteArray cookieDigest_;
};

#endif
