#include "core/HistoryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QSaveFile>
#include <atomic>
#include <stdexcept>

#include "core/JsonHelpers.h"

namespace {
QString sqlText(const QString &value) { return value.isNull() ? QStringLiteral("") : value; }
// Queries die before this guard; release the handle before removing the named connection.
struct ConnectionCleanup {
    QSqlDatabase &database;
    ~ConnectionCleanup() {
        const QString name = database.connectionName();
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
};

void execOrFail(QSqlQuery &query, const char *context) {
    if (!query.exec()) {
        throw std::runtime_error(QString("%1: %2").arg(context, query.lastError().text())
                                         .toStdString());
    }
}

}  // namespace

HistoryStore::HistoryStore(QString databasePath)
    : databasePath_(std::move(databasePath)) {}

QSqlDatabase HistoryStore::createConnection(int timeoutSeconds) {
    static std::atomic_int counter{0};
    const QString name = QString("history-store-%1").arg(++counter);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
    db.setDatabaseName(databasePath_);
    // busy timeout(ms):应用与定时服务并发写同一库时串行化而非 "database is locked"
    db.setConnectOptions(QString("QSQLITE_BUSY_TIMEOUT=%1").arg(timeoutSeconds * 1000));
    if (!db.open()) {
        const QString error = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
        throw std::runtime_error(QString("open database: %1").arg(error).toStdString());
    }
    QSqlQuery pragma(db);
    pragma.exec("PRAGMA journal_mode=WAL;");
    return db;
}

void HistoryStore::initialize() {
    QDir().mkpath(QFileInfo(databasePath_).absolutePath());
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};

    QSqlQuery q(db);
    // Qt SQLITE 驱动不支持一次执行多条语句,逐条执行
    const QStringList statements = {
            R"SQL(CREATE TABLE IF NOT EXISTS history_items (
    item_key TEXT PRIMARY KEY, kid INTEGER NOT NULL, oid INTEGER NOT NULL,
    business TEXT NOT NULL, bvid TEXT NOT NULL, title TEXT NOT NULL, subtitle TEXT NOT NULL,
    cover_url TEXT NOT NULL, author_name TEXT NOT NULL, author_mid INTEGER NOT NULL,
    view_at INTEGER NOT NULL, progress INTEGER NOT NULL, duration INTEGER NOT NULL,
    badge TEXT NOT NULL, link_url TEXT NOT NULL, raw_json TEXT NOT NULL,
    first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL))SQL",
            R"SQL(CREATE TABLE IF NOT EXISTS videos (
    video_key TEXT PRIMARY KEY, kid INTEGER NOT NULL, oid INTEGER NOT NULL,
    business TEXT NOT NULL, bvid TEXT NOT NULL, title TEXT NOT NULL, cover_url TEXT NOT NULL,
    author_name TEXT NOT NULL, author_mid INTEGER NOT NULL, duration INTEGER NOT NULL,
    badge TEXT NOT NULL, link_url TEXT NOT NULL, latest_view_at INTEGER NOT NULL,
    latest_progress INTEGER NOT NULL, latest_subtitle TEXT NOT NULL, latest_raw_json TEXT NOT NULL,
    view_count INTEGER NOT NULL DEFAULT 0, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL))SQL",
            "CREATE INDEX IF NOT EXISTS idx_videos_latest_view_at ON videos(latest_view_at DESC)",
            "CREATE INDEX IF NOT EXISTS idx_videos_author_name ON videos(author_name)",
            "CREATE INDEX IF NOT EXISTS idx_videos_title ON videos(title)",
            R"SQL(CREATE TABLE IF NOT EXISTS view_records (
    id INTEGER PRIMARY KEY AUTOINCREMENT, video_key TEXT NOT NULL, view_at INTEGER NOT NULL,
    progress INTEGER NOT NULL, subtitle TEXT NOT NULL, raw_json TEXT NOT NULL,
    created_at TEXT NOT NULL, UNIQUE(video_key, view_at),
    FOREIGN KEY(video_key) REFERENCES videos(video_key)))SQL",
            "CREATE INDEX IF NOT EXISTS idx_view_records_video_key ON view_records(video_key)",
            "CREATE INDEX IF NOT EXISTS idx_view_records_view_at ON view_records(view_at DESC)",
            R"SQL(CREATE TABLE IF NOT EXISTS sync_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT, started_at TEXT NOT NULL, finished_at TEXT,
    pages INTEGER NOT NULL DEFAULT 0, seen INTEGER NOT NULL DEFAULT 0,
    inserted INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL, message TEXT NOT NULL))SQL",
            R"SQL(CREATE TABLE IF NOT EXISTS cursor_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT, sync_run_id INTEGER NOT NULL, page_index INTEGER NOT NULL,
    max INTEGER NOT NULL, view_at INTEGER NOT NULL, business TEXT NOT NULL, ps INTEGER NOT NULL,
    raw_json TEXT NOT NULL, created_at TEXT NOT NULL,
    FOREIGN KEY(sync_run_id) REFERENCES sync_runs(id)))SQL",
            R"SQL(CREATE TABLE IF NOT EXISTS playback_positions (
    video_key TEXT PRIMARY KEY, position_seconds REAL NOT NULL,
    duration_seconds REAL NOT NULL DEFAULT 0, updated_at TEXT NOT NULL))SQL"};
    for (const QString &statement : statements) {
        QSqlQuery statementQuery(db);
        statementQuery.prepare(statement);
        if (!statementQuery.exec()) {
            throw std::runtime_error(QString("init schema: %1")
                                             .arg(statementQuery.lastError().text())
                                             .toStdString());
        }
    }

    ensureSyncRunSourceColumn(db);
    migrateLegacyItems(db);
}

void HistoryStore::ensureSyncRunSourceColumn(QSqlDatabase &db) {
    QSqlQuery check(db);
    check.prepare("PRAGMA table_info(sync_runs);");
    execOrFail(check, "inspect sync schema");
    bool hasSource = false;
    while (check.next()) {
        if (check.value(1).toString().compare("source", Qt::CaseInsensitive) == 0) {
            hasSource = true;
            break;
        }
    }
    if (hasSource) return;

    QSqlQuery alter(db);
    alter.prepare("ALTER TABLE sync_runs ADD COLUMN source TEXT NOT NULL DEFAULT 'manual';");
    execOrFail(alter, "migrate sync source");
}

void HistoryStore::migrateLegacyItems(QSqlDatabase &db) {
    QSqlQuery countVideos(db);
    countVideos.prepare("SELECT COUNT(*) FROM videos;");
    execOrFail(countVideos, "count migration videos");
    countVideos.next();
    const int videoCount = countVideos.value(0).toInt();
    QSqlQuery countLegacy(db);
    countLegacy.prepare("SELECT COUNT(*) FROM history_items;");
    execOrFail(countLegacy, "count legacy history");
    countLegacy.next();
    const int legacyCount = countLegacy.value(0).toInt();
    if (videoCount > 0 || legacyCount == 0) return;

    if (!db.transaction()) throw std::runtime_error("begin migration transaction failed");
    // videos 表以每视频最近观看的旧行种子化
    QSqlQuery seedVideos(db);
    seedVideos.prepare(R"SQL(
INSERT OR IGNORE INTO videos (
    video_key, kid, oid, business, bvid, title, cover_url, author_name, author_mid,
    duration, badge, link_url, latest_view_at, latest_progress, latest_subtitle,
    latest_raw_json, view_count, first_seen_at, last_seen_at)
SELECT business || ':' || oid || ':' || kid AS video_key,
    kid, oid, business, bvid, title, cover_url, author_name, author_mid,
    duration, badge, link_url, view_at, progress, subtitle, raw_json,
    0, first_seen_at, last_seen_at
FROM history_items h
WHERE view_at = (
    SELECT MAX(view_at) FROM history_items h2
    WHERE h2.business = h.business AND h2.oid = h.oid AND h2.kid = h.kid)
GROUP BY video_key;
)SQL");
    execOrFail(seedVideos, "migrate videos");
    // 每条旧行 = 一个独立 (video, view_at) 观看事件
    QSqlQuery seedRecords(db);
    seedRecords.prepare(R"SQL(
INSERT OR IGNORE INTO view_records (video_key, view_at, progress, subtitle, raw_json, created_at)
SELECT business || ':' || oid || ':' || kid, view_at, progress, subtitle, raw_json, first_seen_at
FROM history_items;
)SQL");
    execOrFail(seedRecords, "migrate view records");
    recomputeViewCounts(db);
    if (!db.commit()) throw std::runtime_error("commit migration failed");
}

QList<HistoryItem> HistoryStore::loadItems(int offset, int limit) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare(R"SQL(
SELECT video_key, kid, oid, business, title, latest_subtitle, cover_url, author_name, author_mid,
       latest_view_at, latest_progress, duration, badge, link_url, latest_raw_json, view_count
FROM videos
ORDER BY latest_view_at DESC, last_seen_at DESC
LIMIT :limit OFFSET :offset;
)SQL");
    q.bindValue(":limit", limit);
    q.bindValue(":offset", offset);
    execOrFail(q, "load items");

    QList<HistoryItem> items;
    while (q.next()) {
        HistoryItem item;
        item.videoKey = q.value(0).toString();
        item.kid = q.value(1).toLongLong();
        item.oid = q.value(2).toLongLong();
        item.business = q.value(3).toString();
        item.title = q.value(4).toString();
        item.subtitle = q.value(5).toString();
        item.coverUrl = q.value(6).toString();
        item.authorName = q.value(7).toString();
        item.authorMid = q.value(8).toLongLong();
        item.viewAt = q.value(9).toLongLong();
        item.progress = q.value(10).toInt();
        item.duration = q.value(11).toInt();
        item.badge = q.value(12).toString();
        item.linkUrl = q.value(13).toString();
        item.rawJson = q.value(14).toString();
        item.viewCount = q.value(15).toInt();
        items.append(item);
    }

    // 只查询已加载视频的事件；导出全库时分批，避免超过 SQLite 绑定参数上限。
    QHash<QString, int> index;
    for (int i = 0; i < items.size(); ++i) index.insert(items[i].videoKey, i);
    constexpr int batchSize = 500;
    for (int first = 0; first < items.size(); first += batchSize) {
        const int end = qMin(first + batchSize, int(items.size()));
        QStringList placeholders;
        for (int i = first; i < end; ++i) placeholders.append("?");
        QSqlQuery records(db);
        records.prepare("SELECT video_key, view_at, progress FROM view_records WHERE video_key IN (" +
                        placeholders.join(',') + ") ORDER BY view_at DESC;");
        for (int i = first; i < end; ++i) records.addBindValue(items[i].videoKey);
        execOrFail(records, "load view records");
        while (records.next()) {
            items[index.value(records.value(0).toString())].viewRecords.append(
                    {records.value(1).toLongLong(), records.value(2).toInt()});
        }
    }
    return items;
}

int HistoryStore::count() {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM videos;");
    execOrFail(q, "count videos");
    q.next();
    return q.value(0).toInt();
}

int HistoryStore::countRecords() {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM view_records;");
    execOrFail(q, "count records");
    q.next();
    return q.value(0).toInt();
}

qint64 HistoryStore::startSyncRun(const QString &source) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare(
            "INSERT INTO sync_runs (started_at, status, message, source) VALUES (:started, "
            "'running', '', :source);");
    q.bindValue(":started", nowIso());
    q.bindValue(":source", source.trimmed().isEmpty() ? QString(SyncSource::Manual)
                                                      : source);
    execOrFail(q, "start sync run");
    QSqlQuery id(db);
    id.exec("SELECT last_insert_rowid();");
    id.next();
    return id.value(0).toLongLong();
}

void HistoryStore::finishSyncRun(qint64 runId, const SyncResult &result,
                                 const QString &status, const QString &message) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare(R"SQL(
UPDATE sync_runs SET finished_at = :finished, pages = :pages, seen = :seen,
    inserted = :inserted, updated = :updated, status = :status, message = :message
WHERE id = :id;
)SQL");
    q.bindValue(":finished", nowIso());
    q.bindValue(":pages", result.pages);
    q.bindValue(":seen", result.seen);
    q.bindValue(":inserted", result.inserted);
    q.bindValue(":updated", result.updated);
    q.bindValue(":status", status);
    q.bindValue(":message", message);
    q.bindValue(":id", runId);
    execOrFail(q, "finish sync run");
}

QList<SyncRunRecord> HistoryStore::listRecentRuns(int limit) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare(R"SQL(
SELECT id, COALESCE(source, 'manual'), started_at, finished_at, pages, seen, inserted, updated,
    status, message FROM sync_runs ORDER BY id DESC LIMIT :limit;
)SQL");
    q.bindValue(":limit", limit);
    execOrFail(q, "list runs");

    QList<SyncRunRecord> runs;
    while (q.next()) {
        SyncRunRecord rec;
        rec.id = q.value(0).toLongLong();
        rec.source = q.value(1).toString();
        rec.startedAt = q.value(2).toString();
        rec.finishedAt = q.value(3).isNull() ? QString() : q.value(3).toString();
        rec.pages = q.value(4).toInt();
        rec.seen = q.value(5).toInt();
        rec.inserted = q.value(6).toInt();
        rec.updated = q.value(7).toInt();
        rec.status = q.value(8).toString();
        rec.message = q.value(9).toString();
        runs.append(rec);
    }
    return runs;
}

QPair<int, int> HistoryStore::upsertItems(qint64 runId, int pageIndex,
                                          const BilibiliHistoryPage &page) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    if (!db.transaction()) {
        throw std::runtime_error("begin transaction failed");
    }

    // 先插 cursor 快照
    QSqlQuery snapshot(db);
    snapshot.prepare(R"SQL(
INSERT INTO cursor_snapshots (sync_run_id, page_index, max, view_at, business, ps, raw_json, created_at)
VALUES (:run, :page, :max, :view_at, :business, :ps, :raw, :created);
)SQL");
    snapshot.bindValue(":run", runId);
    snapshot.bindValue(":page", pageIndex);
    snapshot.bindValue(":max", page.cursor.max);
    snapshot.bindValue(":view_at", page.cursor.viewAt);
    snapshot.bindValue(":business", sqlText(page.cursor.business));
    snapshot.bindValue(":ps", page.cursor.pageSize);
    snapshot.bindValue(":raw", sqlText(page.rawJson));
    snapshot.bindValue(":created", nowIso());
    execOrFail(snapshot, "insert cursor snapshot");

    int inserted = 0;
    int existing = 0;
    QList<QString> touched;

    QSqlQuery videoUpsert(db);
    videoUpsert.prepare(R"SQL(
INSERT INTO videos (
    video_key, kid, oid, business, bvid, title, cover_url, author_name, author_mid,
    duration, badge, link_url, latest_view_at, latest_progress, latest_subtitle,
    latest_raw_json, view_count, first_seen_at, last_seen_at)
VALUES (
    :video_key, :kid, :oid, :business, :bvid, :title, :cover_url, :author_name, :author_mid,
    :duration, :badge, :link_url, :view_at, :progress, :subtitle, :raw_json, 0, :now, :now)
ON CONFLICT(video_key) DO UPDATE SET
    bvid = excluded.bvid,
    title = excluded.title,
    cover_url = excluded.cover_url,
    author_name = excluded.author_name,
    author_mid = excluded.author_mid,
    duration = excluded.duration,
    badge = excluded.badge,
    link_url = excluded.link_url,
    last_seen_at = excluded.last_seen_at,
    latest_view_at = excluded.latest_view_at,
    latest_progress = excluded.latest_progress,
    latest_subtitle = excluded.latest_subtitle,
    latest_raw_json = excluded.latest_raw_json
WHERE excluded.latest_view_at > videos.latest_view_at;
)SQL");

    QSqlQuery recordInsert(db);
    recordInsert.prepare(R"SQL(
INSERT OR IGNORE INTO view_records (video_key, view_at, progress, subtitle, raw_json, created_at)
VALUES (:video_key, :view_at, :progress, :subtitle, :raw_json, :now);
)SQL");

    for (const HistoryItem &item : page.items) {
        const QString videoKey = buildVideoKey(item);
        const QString bvid = extractBvid(item.rawJson);
        const QString now = nowIso();

        videoUpsert.bindValue(":video_key", sqlText(videoKey));
        videoUpsert.bindValue(":kid", item.kid);
        videoUpsert.bindValue(":oid", item.oid);
        videoUpsert.bindValue(":business", sqlText(item.business));
        videoUpsert.bindValue(":bvid", sqlText(bvid));
        videoUpsert.bindValue(":title", sqlText(item.title));
        videoUpsert.bindValue(":cover_url", sqlText(item.coverUrl));
        videoUpsert.bindValue(":author_name", sqlText(item.authorName));
        videoUpsert.bindValue(":author_mid", item.authorMid);
        videoUpsert.bindValue(":duration", item.duration);
        videoUpsert.bindValue(":badge", sqlText(item.badge));
        videoUpsert.bindValue(":link_url", sqlText(item.linkUrl));
        videoUpsert.bindValue(":view_at", item.viewAt);
        videoUpsert.bindValue(":progress", item.progress);
        videoUpsert.bindValue(":subtitle", sqlText(item.subtitle));
        videoUpsert.bindValue(":raw_json", sqlText(item.rawJson));
        videoUpsert.bindValue(":now", now);
        execOrFail(videoUpsert, "upsert video");

        recordInsert.bindValue(":video_key", sqlText(videoKey));
        recordInsert.bindValue(":view_at", item.viewAt);
        recordInsert.bindValue(":progress", item.progress);
        recordInsert.bindValue(":subtitle", sqlText(item.subtitle));
        recordInsert.bindValue(":raw_json", sqlText(item.rawJson));
        recordInsert.bindValue(":now", now);
        execOrFail(recordInsert, "insert view record");

        if (recordInsert.numRowsAffected() > 0) {
            ++inserted;
        } else {
            ++existing;
        }
        if (!touched.contains(videoKey)) touched.append(videoKey);
    }

    recomputeViewCounts(db, touched);
    if (!db.commit()) {
        throw std::runtime_error("commit failed");
    }
    return {inserted, existing};
}

void HistoryStore::recomputeViewCounts(QSqlDatabase &db,
                                       const QList<QString> &videoKeys) {
    QSqlQuery q(db);
    if (videoKeys.isEmpty()) {
        q.prepare(
                "UPDATE videos SET view_count = (SELECT COUNT(*) FROM view_records r "
                "WHERE r.video_key = videos.video_key);");
        execOrFail(q, "recompute all view counts");
        return;
    }
    QString keys;
    for (const QString &key : videoKeys) {
        if (!keys.isEmpty()) keys += ",";
        keys += "'" + QString(key).replace("'", "''") + "'";
    }
    q.prepare(QString(
            "UPDATE videos SET view_count = (SELECT COUNT(*) FROM view_records r WHERE "
            "r.video_key = videos.video_key) WHERE video_key IN (%1);")
                   .arg(keys));
    execOrFail(q, "recompute view counts");
}

void HistoryStore::exportJson(const QString &exportPath) {
    const QList<HistoryItem> items = loadItems();
    QJsonArray array;
    for (const HistoryItem &item : items) {
        QJsonObject obj;
        obj.insert("video_key", item.videoKey);
        obj.insert("kid", static_cast<double>(item.kid));
        obj.insert("oid", static_cast<double>(item.oid));
        obj.insert("business", item.business);
        obj.insert("title", item.title);
        obj.insert("subtitle", item.subtitle);
        obj.insert("cover_url", item.coverUrl);
        obj.insert("author_name", item.authorName);
        obj.insert("author_mid", static_cast<double>(item.authorMid));
        obj.insert("latest_view_at", static_cast<double>(item.viewAt));
        obj.insert("progress", item.progress);
        obj.insert("duration", item.duration);
        obj.insert("badge", item.badge);
        obj.insert("link_url", item.linkUrl);
        obj.insert("view_count", item.viewCount);
        QJsonArray viewAts;
        for (const ViewRecord &record : item.viewRecords) {
            viewAts.append(static_cast<double>(record.viewAt));
        }
        obj.insert("view_ats", viewAts);
        obj.insert("raw_json", item.rawJson);
        array.append(obj);
    }
    QJsonObject payload;
    payload.insert("exported_at", nowIso());
    payload.insert("count", items.size());
    payload.insert("items", array);

    QSaveFile file(exportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("cannot open export file");
    }
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
        throw std::runtime_error("cannot commit history export");
}

std::optional<double> HistoryStore::getPlaybackPosition(const QString &videoKey) {
    QSqlDatabase db = createConnection();
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare("SELECT position_seconds FROM playback_positions WHERE video_key = :key;");
    q.bindValue(":key", sqlText(videoKey));
    execOrFail(q, "get playback position");
    if (!q.next()) return std::nullopt;
    return q.value(0).toDouble();
}

void HistoryStore::savePlaybackPosition(const QString &videoKey, double positionSeconds,
                                        double durationSeconds, int timeoutSeconds) {
    QSqlDatabase db = createConnection(timeoutSeconds);
    ConnectionCleanup cleanup{db};
    QSqlQuery q(db);
    q.prepare(R"SQL(
INSERT INTO playback_positions (video_key, position_seconds, duration_seconds, updated_at)
VALUES (:key, :position, :duration, :updated)
ON CONFLICT(video_key) DO UPDATE SET
    position_seconds = excluded.position_seconds,
    duration_seconds = excluded.duration_seconds,
    updated_at = excluded.updated_at;
)SQL");
    q.bindValue(":key", sqlText(videoKey));
    q.bindValue(":position", positionSeconds);
    q.bindValue(":duration", durationSeconds);
    q.bindValue(":updated", nowIso());
    execOrFail(q, "save playback position");
}

QString HistoryStore::buildVideoKey(const HistoryItem &item) {
    return item.business + ":" + QString::number(item.oid) + ":" + QString::number(item.kid);
}

QString HistoryStore::extractBvid(const QString &rawJson) {
    if (rawJson.trimmed().isEmpty()) return QString();
    const QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8());
    if (!doc.isObject()) return QString();
    return jh::getString(jh::getChild(doc.object(), "history"), "bvid");
}

QString HistoryStore::nowIso() {
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}
