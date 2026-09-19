// HistoryStore 无头自测:initialize / 幂等 upsert / 分页 / view_count 重算 /
// 导出 / 播放位置 UPSERT。退出码 0 = 全部通过。
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cstdio>
#include <stdexcept>

#include "core/HistoryStore.h"

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (condition) {
        std::printf("PASS %s\n", what);
    } else {
        failures++;
        std::printf("FAIL %s\n", what);
    }
    std::fflush(stdout);
}

HistoryItem makeItem(const QString &business, qint64 oid, qint64 kid, qint64 viewAt,
                     int progress) {
    HistoryItem item;
    item.business = business;
    item.oid = oid;
    item.kid = kid;
    item.videoKey = business + ":" + QString::number(oid) + ":" + QString::number(kid);
    item.title = "测试视频 " + QString::number(oid);
    item.subtitle = "P1";
    item.coverUrl = "https://i0.hdslb.com/bfs/archive/test.jpg";
    item.authorName = "UP 主";
    item.authorMid = 42;
    item.viewAt = viewAt;
    item.progress = progress;
    item.duration = 600;
    item.badge = "视频";
    item.linkUrl = "https://www.bilibili.com/video/BV1xx";
    item.rawJson = QString(R"({"history":{"business":"%1","oid":%2,"kid":%3,"bvid":"BV1xx"}})")
                           .arg(business)
                           .arg(oid)
                           .arg(kid);
    return item;
}

BilibiliHistoryPage makePage(const QList<HistoryItem> &items, qint64 max) {
    BilibiliHistoryPage page;
    page.cursor = {max, 1700000000, "archive", 30};
    page.items = items;
    page.rawJson = "{}";
    return page;
}

}  // namespace

int main(int argc, char *argv[]) {
    std::printf("store-test starting\n");
    std::fflush(stdout);
    QCoreApplication app(argc, argv);

    const QString dir = QDir::currentPath() + "/bbhouse-store-test-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(dir);
    const QString dbPath = dir + "/test.sqlite3";

    try {
        HistoryStore store(dbPath);
        store.initialize();

        // 首次 upsert:2 视频 2 记录(先建审计行)
        const qint64 run1 = store.startSyncRun(QString(SyncSource::Manual));
        const qint64 run2 = store.startSyncRun(QString(SyncSource::Manual));
        const qint64 run3 = store.startSyncRun(QString(SyncSource::Scheduled));
        auto first = store.upsertItems(run1, 1, makePage({makeItem("archive", 100, 100, 1700000100, 30),
                                                          makeItem("archive", 200, 200, 1700000200, 0)},
                                                         0));
        check(first.first == 2 && first.second == 0, "首次 upsert 新增 2 条观看记录");

        // 重同步幂等:同 (video_key, view_at) 不再新增
        auto repeat = store.upsertItems(run2, 1, makePage({makeItem("archive", 100, 100, 1700000100, 30)}, 0));
        check(repeat.first == 0 && repeat.second == 1, "重同步幂等(同时间戳不重复)");

        // 新观看时间戳 → 新记录 + 视频展示字段前移
        auto again = store.upsertItems(run3, 1, makePage({makeItem("archive", 100, 100, 1700009900, 120)}, 0));
        check(again.first == 1, "新观看时间戳新增记录");

        const QList<HistoryItem> items = store.loadItems();
        check(items.size() == 2, "共 2 个视频");
        check(items[0].oid == 100, "视频 100 排最前(最近观看)");
        check(items[0].viewCount == 2, "视频 100 观看计数 = 2");
        check(items[0].viewRecords.size() == 2, "视频 100 装载 2 条观看事件");
        check(items[0].viewRecords[0].viewAt == 1700009900, "观看事件按时间倒序");
        check(items[0].progress == 120, "展示进度 = 最近一次");
        check(store.count() == 2 && store.countRecords() == 3, "计数 2 视频 / 3 记录");

        // 游标快照与审计
        const auto runs = store.listRecentRuns(10);
        check(runs.size() == 3, "审计行 3 条");
        check(runs[0].status == "running", "最近一次 running(未收尾)");

        // 播放位置
        check(store.getPlaybackPosition("archive:100:100") == std::nullopt, "未播过的视频无位置");
        store.savePlaybackPosition("archive:100:100", 123.5, 600);
        check(store.getPlaybackPosition("archive:100:100").value_or(-1) == 123.5, "播放位置写入");
        store.savePlaybackPosition("archive:100:100", 0, 600);
        check(store.getPlaybackPosition("archive:100:100").value_or(-1) == 0, "播放位置覆写(0=看完)");

        // 导出
        const QString exportPath = dir + "/export.json";
        store.exportJson(exportPath);
        check(QFile::exists(exportPath), "JSON 导出文件存在");
        QFile exportFile(exportPath);
        if (!exportFile.open(QIODevice::ReadOnly)) {
            failures++;
            std::printf("FAIL 导出文件不可读\n");
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(exportFile.readAll());
            check(doc.object().value("count").toInt() == 2, "导出计数正确");
        }

        // 二次 initialize 幂等(建表 IF NOT EXISTS + 迁移跳过)
        store.initialize();
        check(store.count() == 2, "二次 initialize 幂等");
    } catch (const std::exception &e) {
        failures++;
        std::printf("FAIL 异常: %s\n", e.what());
    }

    QDir(dir).removeRecursively();
    std::printf(failures == 0 ? "ALL PASS\n" : "FAILED %d\n", failures);
    return failures == 0 ? 0 : 1;
}
