#ifndef DYNAMIC_CARD_MODEL_H
#define DYNAMIC_CARD_MODEL_H

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

// 动态投影的稳定模型：分区补查仅更新对应行，保留 QML 卡片与封面实例。
class DynamicCardModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { CardDataRole = Qt::UserRole + 1 };
    explicit DynamicCardModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 输入是完整目标快照；返回是否发生变化。相同快照不会发送任何信号。
    bool setItems(const QVariantList &items);

signals:
    void countChanged();

private:
    struct Row {
        QString key;
        QVariantMap card;
    };
    QList<Row> rows_;
};

#endif  // DYNAMIC_CARD_MODEL_H
