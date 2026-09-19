#include "DynamicCardModel.h"

#include <QSet>

namespace {
QString identity(const QVariantMap &card, int row) {
    const bool video = card.value("category").toString() == QLatin1String("video");
    bool validAid = false;
    const qint64 aid = card.value("aid").toLongLong(&validAid);
    if (video && validAid && aid > 0) return QStringLiteral("video:%1").arg(aid);
    const QString id = card.value("id").toString();
    if (!id.isEmpty()) return QStringLiteral("dynamic:") + id;
    // 畸形数据没有可用身份时按位置隔离；不与正常 aid/id 键冲突。
    return QStringLiteral("missing:%1").arg(row);
}
}

DynamicCardModel::DynamicCardModel(QObject *parent) : QAbstractListModel(parent) {}

int DynamicCardModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(rows_.size());
}

int DynamicCardModel::count() const { return rowCount(); }

QVariant DynamicCardModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() != 0 ||
        index.row() < 0 || index.row() >= rows_.size() || role != CardDataRole) return {};
    return rows_[index.row()].card;
}

QHash<int, QByteArray> DynamicCardModel::roleNames() const {
    return {{CardDataRole, "cardData"}};
}

bool DynamicCardModel::setItems(const QVariantList &items) {
    QList<Row> target;
    target.reserve(items.size());
    QHash<QString, int> occurrences;
    QSet<QString> targetKeys;
    for (int i = 0; i < items.size(); ++i) {
        const QVariantMap card = items[i].toMap();
        const QString base = identity(card, i);
        // 重复身份按出现顺序分开处理，不丢卡片，也不让模型出现重复内部键。
        // 长度前缀避免动态 id 自带分隔符时与 occurrence 后缀产生歧义。
        const QString key = QStringLiteral("%1:%2:%3").arg(base.size()).arg(base).arg(occurrences[base]++);
        target.append({key, card});
        targetKeys.insert(key);
    }

    const int previousCount = count();
    bool changed = false;
    for (int i = count() - 1; i >= 0; --i) {
        if (targetKeys.contains(rows_[i].key)) continue;
        beginRemoveRows({}, i, i);
        rows_.removeAt(i);
        endRemoveRows();
        changed = true;
    }

    for (int i = 0; i < target.size(); ++i) {
        int existing = i;
        while (existing < count() && rows_[existing].key != target[i].key) ++existing;
        if (existing == count()) {
            beginInsertRows({}, i, i);
            rows_.insert(i, target[i]);
            endInsertRows();
            changed = true;
        } else {
            if (existing != i) {
                beginMoveRows({}, existing, existing, {}, i);
                rows_.move(existing, i);
                endMoveRows();
                changed = true;
            }
            if (rows_[i].card != target[i].card) {
                rows_[i].card = target[i].card;
                emit dataChanged(index(i, 0), index(i, 0), {CardDataRole});
                changed = true;
            }
        }
    }
    if (previousCount != count()) emit countChanged();
    return changed;
}
