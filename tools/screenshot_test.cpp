#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include "player/MpvClient.h"
#include "player/MpvLib.h"
#include "player/ScreenshotPath.h"

int runScreenshotCompressionTests();

#ifdef Q_OS_MACOS
int runMacScreenshotTest(int argc, char **argv);
#endif

namespace {
int failures = 0;
void check(bool ok, const char *name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
bool until(const std::function<bool()> &predicate) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}
}

int main(int argc, char **argv) {
#ifdef Q_OS_MACOS
    if (argc > 1 && QByteArray(argv[1]) == "--macos-opengl")
        return runMacScreenshotTest(argc, argv);
#endif
    QCoreApplication app(argc, argv);
    failures += runScreenshotCompressionTests();
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/screenshot-test-XXXXXX");
    check(temp.isValid(), "fixtures remain under build directory");
    QSet<QString> paths;
    for (int i = 0; i < 1000; ++i) paths.insert(ScreenshotPath::next(temp.path(), "title/:*?"));
    check(paths.size() == 1000, "rapid requests have distinct timestamp filenames");
    bool pathsValid = true;
    for (const auto &path : paths) pathsValid &= QFileInfo(path).absolutePath() == temp.path();
    check(pathsValid, "filename sanitization prevents path traversal");
    auto *client = MpvClient::create();
    check(client != nullptr, "real mpv initializes");
    if (!client) return 1;
    int failuresDelivered = 0;
    bool returned = false;
    client->commandAsync({"invalid-bbhouse-command"}, &app, [&](int result) {
        check(returned && result < 0, "submission failure completes asynchronously");
        ++failuresDelivered;
    });
    returned = true;
    check(until([&] { return failuresDelivered == 1; }), "submission failure delivered exactly once");
    const auto emptyPath = ScreenshotPath::next(temp.path(), "no-video");
    int noVideoResult = 0;
    bool noVideoDone = false;
    client->commandAsync({"screenshot-to-file", emptyPath, "video"}, &app, [&](int result) {
        noVideoResult = result;
        noVideoDone = true;
    });
    check(until([&] { return noVideoDone; }) && noVideoResult < 0 && !QFile::exists(emptyPath),
          "no-frame screenshot reports actual mpv failure without a fake file");

    bool staleCallback = false;
    auto *context = new QObject;
    client->commandAsync({"invalid-bbhouse-command"}, context, [&](int) { staleCallback = true; });
    delete context;
    QCoreApplication::processEvents();
    check(!staleCallback, "destroyed callback receiver is not accessed");

    // vo=null permits a real decoded video frame without any interactive window.
    client->setPropertyString("vo", "null");
    client->setPropertyString("ao", "null");
    QImage source(128, 72, QImage::Format_RGB32);
    source.fill(QColor(200, 30, 50));
    const QString input = temp.path() + "/source.png";
    check(source.save(input), "create local video-frame fixture");
    client->loadSingle(input, 0);
    check(until([&] { return client->getPropertyDouble("video-out-params/w") == 128; }),
          "local frame decoded with headless video output");
    const QString subtitles = temp.path() + "/overlay.srt";
    QFile subtitleFile(subtitles);
    check(subtitleFile.open(QIODevice::WriteOnly), "create subtitle exclusion fixture");
    subtitleFile.write("1\n00:00:00,000 --> 00:10:00,000\nSUBTITLE MUST NOT APPEAR\n");
    subtitleFile.close();
    check(client->command({"sub-add", subtitles, "select"}) >= 0,
          "attach subtitle track before pure-video capture");
    const QString first = ScreenshotPath::next(temp.path(), "capture");
    const QString second = ScreenshotPath::next(temp.path(), "capture");
    int completed = 0;
    QSet<QString> completedPaths;
    for (const auto &path : {first, second}) {
        client->commandAsync({"screenshot-to-file", path, "video"}, &app, [&, path](int result) {
            QImage image(path);
            check(result >= 0 && image.size() == source.size(),
                  "command reply arrives after decodable video PNG is written");
            int different = 0;
            if (image.size() == source.size()) {
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const auto pixel = image.pixelColor(x, y);
                        if (qAbs(pixel.red() - 200) > 5 || qAbs(pixel.green() - 30) > 5 ||
                            qAbs(pixel.blue() - 50) > 5) ++different;
                    }
                }
            }
            check(!image.isNull() && different == 0, "video capture excludes subtitle pixels");
            completedPaths.insert(path);
            ++completed;
        });
    }
    check(until([&] { return completed == 2; }) && completedPaths.size() == 2,
          "concurrent screenshot completions stay paired with their paths");
    check(failuresDelivered == 1, "submission error does not produce a duplicate reply");
    client->stop();
    delete client;
    return failures ? 1 : 0;
}
