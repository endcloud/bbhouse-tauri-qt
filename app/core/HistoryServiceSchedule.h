#ifndef HISTORY_SERVICE_SCHEDULE_H
#define HISTORY_SERVICE_SCHEDULE_H

#include "core/HistoryScheduler.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QMap>

// A high-water date per schedule prevents duplicate attempts after restart, DST
// fallback and wall-clock rollback. Changing a schedule creates a distinct key;
// changing it back does not forget its previous attempt.
namespace HistoryServiceSchedule {
inline QString key(const HistoryServiceConfig &config) {
    const QByteArray plan = config.cycle.toUtf8() + '\n' + config.time.toUtf8() + '\n'
        + QByteArray::number(config.cycle == "weekly" ? config.weekday : 0);
    return QString::fromLatin1(QCryptographicHash::hash(plan, QCryptographicHash::Sha256).toHex());
}
inline bool due(const HistoryServiceConfig &config, const QDateTime &now,
                const QMap<QString, QDate> &attempts) {
    if (!config.enabled || !now.isValid() || now.time().toString("HH:mm") != config.time)
        return false;
    if (config.cycle == "weekly" && now.date().dayOfWeek() != config.weekday) return false;
    const QDate previous = attempts.value(key(config));
    return !previous.isValid() || now.date() > previous;
}
}

#endif
