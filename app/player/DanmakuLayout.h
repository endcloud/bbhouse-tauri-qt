#ifndef DANMAKU_LAYOUT_H
#define DANMAKU_LAYOUT_H

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QSizeF>
#include <QTextLayout>
#include <QVariantList>
#include <QVector>
#include <memory>

// GUI-thread timeline/layout model. Text layouts are immutable after admission;
// the renderer reads the snapshot only during Qt Quick's synchronization phase.
class DanmakuLayout {
public:
    struct Visual {
        quint64 id = 0;
        std::shared_ptr<QTextLayout> text;
        QColor color;
        QPointF position;
        QSizeF size;
    };
    static QFont platformFont(int pixels);
    void load(const QVariantList &entries);
    void configure(QSizeF viewport, int pixels, int speedPercent, int areaPercent);
    void setDensityLimit(int value);
    void reset(double time);
    const QVector<Visual> &advance(double time);
    const QVector<Visual> &frame() const { return frame_; }
    double nextTime() const;
    quint64 generation() const { return generation_; }
    quint64 preparedCount() const { return preparedCount_; }
    int activeCount() const { return active_.size(); }
    int laneCount() const { return scrollLines_.size(); }

private:
    struct Entry {
        double time;
        int type;
        double scale;
        QColor color;
        QString message;
    };
    struct Active {
        Visual visual;
        double start;
        double end;
        double velocity;
        bool centered;
    };
    QVector<Entry> entries_;
    QVector<Active> active_;
    QVector<Visual> frame_;
    QVector<QPair<double, double>> scrollLines_;
    QVector<double> centerLines_;
    QSizeF viewport_;
    QFont font_ = platformFont(25);
    int pixels_ = 25;
    int speedPercent_ = 100;
    int areaPercent_ = 100;
    int densityLimit_ = 0;
    int next_ = 0;
    double maxScale_ = 1;
    double lineHeight_ = 30;
    double time_ = 0;
    quint64 generation_ = 0;
    quint64 preparedCount_ = 0;
};
#endif
