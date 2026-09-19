#ifndef POPULAR_CONTROLLER_H
#define POPULAR_CONTROLLER_H

#include <QHash>
#include <QObject>
#include <QVariantList>

#include "core/PopularApi.h"

// 每种榜单和每周期数独立缓存；切换选择会使旧请求失效。
class PopularController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList pool READ pool NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString description READ description NOTIFY stateChanged)
    Q_PROPERTY(QString activeTab READ activeTab NOTIFY stateChanged)
    Q_PROPERTY(int rankingRid READ rankingRid NOTIFY stateChanged)
    Q_PROPERTY(int weeklyNumber READ weeklyNumber NOTIFY stateChanged)
    Q_PROPERTY(QVariantList weeklyPeriods READ weeklyPeriods NOTIFY stateChanged)
    Q_PROPERTY(bool periodsBusy READ periodsBusy NOTIFY stateChanged)
    Q_PROPERTY(QString periodsError READ periodsError NOTIFY stateChanged)
    Q_PROPERTY(bool periodsLoaded READ periodsLoaded NOTIFY stateChanged)
public:
    explicit PopularController(QObject *parent = nullptr);
    QVariantList pool() const { return state().items; }
    bool busy() const { return state().busy; }
    bool loaded() const { return state().loaded; }
    bool hasMore() const { return activeTab_ == "popular" && state().hasMore; }
    QString error() const { return state().error; }
    QString description() const { return state().description; }
    QString activeTab() const { return activeTab_; }
    int rankingRid() const { return rankingRid_; }
    int weeklyNumber() const { return weeklyNumber_; }
    QVariantList weeklyPeriods() const { return weeklyPeriods_; }
    bool periodsBusy() const { return periodsBusy_; }
    QString periodsError() const { return periodsError_; }
    bool periodsLoaded() const { return periodsLoaded_; }

    Q_INVOKABLE void ensureLoaded();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void selectTab(const QString &tab);
    Q_INVOKABLE void selectRanking(int rid);
    Q_INVOKABLE void selectWeek(int number);
    Q_INVOKABLE void refreshPeriods();
signals:
    void stateChanged();
private:
    friend class PopularControllerTest;
    struct State {
        QVariantList items;
        QString error;
        QString description;
        bool busy = false;
        bool loaded = false;
        bool hasMore = true;
        int nextPage = 1;
    };
    static constexpr int kMaxPagesPerOperation = 100;
    QString key() const;
    const State &state() const;
    void invalidateFetch();
    void beginFetch(bool replace);
    virtual void startFetch(quint64 generation, const QString &selectionKey, int page);
    virtual void startPeriodsFetch(quint64 generation);
    void finishFetch(quint64 generation, const QString &selectionKey, int page,
                     const PopularPage &result, const QString &error);
    void finishPeriodsFetch(quint64 generation, const QVariantList &periods, const QString &error);

    QHash<QString, State> cache_;
    QString activeTab_ = QStringLiteral("popular");
    int rankingRid_ = 0;
    int weeklyNumber_ = 0;
    QVariantList weeklyPeriods_;
    bool periodsBusy_ = false;
    bool periodsLoaded_ = false;
    QString periodsError_;
    quint64 generation_ = 0;
    quint64 periodsGeneration_ = 0;
    bool replace_ = true;
    int pendingPage_ = 1;
    int scannedPages_ = 0;
};

#endif
