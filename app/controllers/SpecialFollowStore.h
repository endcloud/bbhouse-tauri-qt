#ifndef SPECIAL_FOLLOW_STORE_H
#define SPECIAL_FOLLOW_STORE_H

#include <QMutex>
#include <QString>
#include <QList>

// 本地特别关注 UP 快照(契约见 openspec/specs/special-follow-ui):JSON 文件
// 持久化于数据目录(special-follow.json),与服务端"特别关注分组"解耦 ——
// 服务端分组仅在文件不存在时做一次性种子导入,此后本地列表独立演进。
// 结构:[{"mid":..,"name":..,"face":..,"addedAt":..}]
// 读写互斥(调用方在池线程),文件损坏回退空表(fileExists=true,不再重播种,
// 避免覆盖用户手改残留);零 SQLite 写入(规约:MUST NOT 落云端历史库)。
class SpecialFollowStore {
   public:
    struct Entry {
        qint64 mid = 0;
        QString name;
        QString faceUrl;
        qint64 addedAt = 0;  // 加入时间(Unix 秒)
    };

    // fileExists=false 仅当数据目录尚无该文件(种子导入的唯一触发条件)
    struct Snapshot {
        bool fileExists = false;
        QList<Entry> members;
    };

    Snapshot load();
    // 原子写(QSaveFile);目录不存在时先建
    bool save(const QList<Entry> &members);

   private:
    QMutex mutex_;  // 读写串行化(load/save 均可能来自池线程)
};

#endif  // SPECIAL_FOLLOW_STORE_H
