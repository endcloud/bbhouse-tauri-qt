#pragma once
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Connection per operation; safe for isolated tests and future worker callers.
class DownloadStore {
public:
    explicit DownloadStore(QString path);
    void initialize();
    QVariantList load() const;
    void save(const QVariantMap &record) const;
    void remove(const QString &id) const;
private:
    QString path_;
};
