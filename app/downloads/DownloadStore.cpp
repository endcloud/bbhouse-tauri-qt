#include "downloads/DownloadStore.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <stdexcept>

namespace {
class Connection {
public:
    explicit Connection(const QString &path)
        : name_(QUuid::createUuid().toString()), db_(QSqlDatabase::addDatabase("QSQLITE", name_)) {
        db_.setDatabaseName(path);
        db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db_.open()) throw std::runtime_error("Cannot open downloads database");
    }
    ~Connection() {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }
    QSqlDatabase &db() { return db_; }
private:
    QString name_;
    QSqlDatabase db_;
};
void checked(QSqlQuery &query) {
    if (!query.exec()) throw std::runtime_error("Downloads database operation failed");
}
}

DownloadStore::DownloadStore(QString path) : path_(std::move(path)) {}
void DownloadStore::initialize() {
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
        throw std::runtime_error("Cannot create downloads database directory");
    Connection connection(path_);
    QSqlQuery query(connection.db());
    query.prepare("PRAGMA journal_mode=WAL");
    checked(query);
    query.prepare("CREATE TABLE IF NOT EXISTS downloads (id TEXT PRIMARY KEY, "
                  "created_at TEXT NOT NULL, record_json TEXT NOT NULL)");
    checked(query);
}
QVariantList DownloadStore::load() const {
    Connection connection(path_);
    QSqlQuery query(connection.db());
    query.prepare("SELECT record_json FROM downloads ORDER BY created_at DESC, id DESC");
    checked(query);
    QVariantList rows;
    while (query.next()) {
        const auto document = QJsonDocument::fromJson(query.value(0).toByteArray());
        if (document.isObject()) rows.append(document.object().toVariantMap());
    }
    return rows;
}
void DownloadStore::save(const QVariantMap &record) const {
    // Record construction is restricted by the controller; SQL has no API payload columns.
    Connection connection(path_);
    QSqlQuery query(connection.db());
    query.prepare("INSERT INTO downloads (id, created_at, record_json) VALUES (?, ?, ?) "
                  "ON CONFLICT(id) DO UPDATE SET record_json=excluded.record_json");
    query.addBindValue(record.value("id").toString());
    query.addBindValue(record.value("createdAt").toString());
    query.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(record))
                                          .toJson(QJsonDocument::Compact)));
    checked(query);
}

void DownloadStore::remove(const QString &id) const {
    Connection connection(path_);
    QSqlQuery query(connection.db());
    query.prepare("DELETE FROM downloads WHERE id = ?");
    query.addBindValue(id);
    checked(query);
}
