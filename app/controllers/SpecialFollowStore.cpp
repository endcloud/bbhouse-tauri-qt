#include "controllers/SpecialFollowStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/AppPaths.h"
#include "core/JsonHelpers.h"

namespace {
// 存储文件名(数据目录下与 sqlite 库并列;不入库是规约要求)
const QString kFileName = QStringLiteral("special-follow.json");

QJsonObject entryToJson(const SpecialFollowStore::Entry &entry) {
    QJsonObject object;
    object.insert("mid", entry.mid > 9007199254740991LL
                             ? QJsonValue(QString::number(entry.mid))
                             : QJsonValue(static_cast<double>(entry.mid)));
    object.insert("name", entry.name);
    object.insert("face", entry.faceUrl);
    object.insert("addedAt", static_cast<double>(entry.addedAt));
    return object;
}

// 单条容错:mid 缺失/非正即弃(不因个别脏行丢整表)
bool entryFromJson(const QJsonObject &object, SpecialFollowStore::Entry *entry) {
    const qint64 mid = jh::getInt64(object, "mid");
    if (mid <= 0) return false;
    entry->mid = mid;
    entry->name = object.value("name").toString();
    entry->faceUrl = object.value("face").toString();
    entry->addedAt = static_cast<qint64>(object.value("addedAt").toDouble(0));
    return true;
}
}  // namespace

SpecialFollowStore::Snapshot SpecialFollowStore::load() {
    QMutexLocker locker(&mutex_);
    Snapshot snapshot;
    const QString path = AppPaths::dataDir() + QLatin1Char('/') + kFileName;
    QFile file(path);
    if (!file.exists()) return snapshot;  // fileExists=false → 种子导入触发条件
    snapshot.fileExists = true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return snapshot;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    // 损坏(解析失败/非数组)回退空表:存在标志仍为 true,不重播种
    if (!document.isArray()) return snapshot;
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) continue;
        SpecialFollowStore::Entry entry;
        if (entryFromJson(value.toObject(), &entry)) snapshot.members.append(entry);
    }
    return snapshot;
}

bool SpecialFollowStore::save(const QList<Entry> &members) {
    QMutexLocker locker(&mutex_);
    QDir().mkpath(AppPaths::dataDir());
    QJsonArray array;
    for (const Entry &entry : members) {
        array.append(entryToJson(entry));
    }
    QSaveFile file(AppPaths::dataDir() + QLatin1Char('/') + kFileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return file.commit();
}
