// Synthetic raster comparison, not an end-to-end player benchmark.
// Both paths use identical cached outlines, DPR, positions and frame clearing.
// Text layout/cache creation and Qt Quick texture upload are outside the timing.
#include <QElapsedTimer>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <algorithm>
#include <vector>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QFont font;
    font.setPixelSize(25);
    font.setBold(true);
    qInfo() << "Default family:" << QFontInfo(font).family();
    QFont ping(QStringLiteral("PingFang SC"));
    ping.setPixelSize(25);
    qInfo() << "Requested PingFang SC resolved:" << QFontInfo(ping).family();

    const QString text = QStringLiteral("弹幕性能测试：你好世界 Qt 6.11");
    const QPen outline(Qt::black, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPainterPath path;
    path.addText(2, QFontMetricsF(font).ascent() + 2, font, text);
    constexpr int warmup = 20;
    constexpr int samples = 200;
    for (int dpr : {1, 2}) {
        QImage sprite(QSize(520, 42) * dpr, QImage::Format_ARGB32_Premultiplied);
        sprite.setDevicePixelRatio(dpr);
        sprite.fill(Qt::transparent);
        {
            QPainter painter(&sprite);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(outline);
            painter.setBrush(Qt::white);
            painter.drawPath(path);
        }
        QImage frame(QSize(1920, 1080) * dpr, QImage::Format_ARGB32_Premultiplied);
        frame.setDevicePixelRatio(dpr);
        for (bool cached : {false, true}) {
            std::vector<double> times;
            times.reserve(samples);
            for (int n = 0; n < warmup + samples; ++n) {
                QElapsedTimer timer;
                timer.start();
                frame.fill(Qt::transparent);
                {
                    QPainter painter(&frame);
                    painter.setRenderHint(QPainter::Antialiasing);
                    painter.setRenderHint(QPainter::TextAntialiasing);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    painter.setPen(outline);
                    painter.setBrush(Qt::white);
                    for (int i = 0; i < 32; ++i) {
                        painter.save();
                        painter.translate(10 + (i % 3) * 540 + n * 0.37, (i / 3) * 80);
                        if (cached) painter.drawImage(QPointF(0, 0), sprite);
                        else painter.drawPath(path);
                        painter.restore();
                    }
                }
                if (n >= warmup) times.push_back(timer.nsecsElapsed() / 1e6);
            }
            std::sort(times.begin(), times.end());
            qInfo() << "DPR" << dpr << (cached ? "cached image" : "cached path")
                    << "median ms" << times[samples / 2]
                    << "p95 ms" << times[samples * 95 / 100];
        }
    }
}
