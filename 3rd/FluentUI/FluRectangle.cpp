#include "FluRectangle.h"
#include <QPainterPath>

FluRectangle::FluRectangle(QQuickItem *parent) : QQuickPaintedItem(parent) {
    color(QColor(255, 255, 255, 255));
    radius({0, 0, 0, 0});
    connect(this, &FluRectangle::colorChanged, this, [=] { update(); });
    connect(this, &FluRectangle::radiusChanged, this, [=] { update(); });
}


void FluRectangle::paint(QPainter *painter) {
    // QML 侧 radius 常给单值(radius: 20 → 长度 1 的 QList),原实现直接
    // 取下标 [1..3] 越界,在启用断言的 Qt 官方构建上 SIGABRT。此处按
    // "缺位补末值"归一化到 4 角(0→全 0,1→四角同值,2/3→末值顺延)。
    QList<int> r = _radius;
    while (r.size() < 4) r.append(r.isEmpty() ? 0 : r.constLast());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    QRectF rect = boundingRect();
    path.moveTo(rect.bottomRight() - QPointF(0, r[2]));
    path.lineTo(rect.topRight() + QPointF(0, r[1]));
    path.arcTo(QRectF(QPointF(rect.topRight() - QPointF(r[1] * 2, 0)),
                      QSize(r[1] * 2, r[1] * 2)),
               0, 90);
    path.lineTo(rect.topLeft() + QPointF(r[0], 0));
    path.arcTo(QRectF(QPointF(rect.topLeft()), QSize(r[0] * 2, r[0] * 2)), 90, 90);
    path.lineTo(rect.bottomLeft() - QPointF(0, r[3]));
    path.arcTo(QRectF(QPointF(rect.bottomLeft() - QPointF(0, r[3] * 2)),
                      QSize(r[3] * 2, r[3] * 2)),
               180, 90);
    path.lineTo(rect.bottomRight() - QPointF(r[2], 0));
    path.arcTo(QRectF(QPointF(rect.bottomRight() - QPointF(r[2] * 2, r[2] * 2)),
                      QSize(r[2] * 2, r[2] * 2)),
               270, 90);
    painter->fillPath(path, _color);
    painter->restore();
}
