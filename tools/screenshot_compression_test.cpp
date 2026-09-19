#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <iostream>
#include "player/ScreenshotWriter.h"

int runScreenshotCompressionTests() {
    int failures = 0;
    const auto check = [&](bool ok, const char *name) {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        failures += !ok;
    };
    const auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/screenshot-compression-XXXXXX");
    check(temp.isValid(), "compression fixtures stay under build and are automatically removed");
    if (!temp.isValid()) return failures;

    QImage small(128, 72, QImage::Format_RGB32);
    small.fill(QColor(100, 180, 30));
    const QString smallSource = temp.filePath("small-source.png");
    check(small.save(smallSource), "write small lossless PNG fixture");
    const auto smallBytes = read(smallSource);
    const auto retained = ScreenshotWriter::save(smallSource, temp.filePath("small.png"));
    check(retained.error.isEmpty() && retained.path.endsWith(".png") &&
              read(retained.path) == smallBytes,
          "small PNG retains exact original bytes and PNG suffix");

    QFile padded(smallSource);
    check(padded.open(QIODevice::Append) &&
              padded.write(QByteArray(ScreenshotWriter::MaxBytes - 1 - smallBytes.size(), '\0')) ==
                  ScreenshotWriter::MaxBytes - 1 - smallBytes.size(),
          "pad valid PNG to one byte below the limit");
    padded.close();
    const auto belowLimit = ScreenshotWriter::save(smallSource, temp.filePath("below-limit.png"));
    check(belowLimit.error.isEmpty() && belowLimit.path.endsWith(".png") &&
              QFileInfo(belowLimit.path).size() == ScreenshotWriter::MaxBytes - 1 &&
              read(belowLimit.path) == read(smallSource),
          "PNG one byte below limit is preserved without recompression");
    check(padded.open(QIODevice::Append) && padded.write(QByteArray(1, '\0')) == 1,
          "pad valid PNG to exactly the maximum byte boundary");
    padded.close();
    const auto boundary = ScreenshotWriter::save(smallSource, temp.filePath("boundary.png"));
    check(boundary.error.isEmpty() && boundary.path.endsWith(".jpg") &&
              QFileInfo(boundary.path).size() < ScreenshotWriter::MaxBytes &&
              QImage(boundary.path).size() == small.size(),
          "exact byte boundary is compressed into a valid JPEG below limit");

    QImage noise(3840, 2160, QImage::Format_RGB32);
    quint32 random = 0x12345678;
    for (int y = 0; y < noise.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(noise.scanLine(y));
        for (int x = 0; x < noise.width(); ++x) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            pixels[x] = 0xff000000 | (random & 0x00ffffff);
        }
    }
    const QString noiseSource = temp.filePath("noise-source.png");
    check(noise.save(noiseSource) && QFileInfo(noiseSource).size() > ScreenshotWriter::MaxBytes,
          "4K high-entropy fixture exceeds 3 MiB");
    const auto compressed = ScreenshotWriter::save(noiseSource, temp.filePath("noise.png"));
    QImageReader compressedReader(compressed.path);
    const auto compressedFormat = compressedReader.format();
    const auto decoded = compressedReader.read();
    check(compressed.error.isEmpty() && compressed.path.endsWith(".jpg") &&
              QFileInfo(compressed.path).size() > 0 &&
              QFileInfo(compressed.path).size() < ScreenshotWriter::MaxBytes &&
              (compressedFormat == "jpeg" || compressedFormat == "jpg") && !decoded.isNull() &&
              decoded.width() <= noise.width() && decoded.height() <= noise.height() &&
              qAbs(double(decoded.width()) / decoded.height() - 16.0 / 9) < 0.01,
          "4K high-entropy output is a decodable JPEG below 3 MiB with preserved aspect");
    check(QFile::exists(noiseSource), "compression never removes its caller-owned input");

    const QImage medium = noise.scaled(512, 288);
    const QString mediumSource = temp.filePath("medium-source.png");
    check(medium.save(mediumSource), "write resize budget fixture");
    QByteArray qualityFifty;
    QBuffer qualityBuffer(&qualityFifty);
    check(qualityBuffer.open(QIODevice::WriteOnly) && medium.save(&qualityBuffer, "JPEG", 50),
          "derive a budget that permits JPEG at original resolution");
    const auto preservedSize = ScreenshotWriter::save(
        mediumSource, temp.filePath("preserved-resolution.png"), qualityFifty.size() + 1);
    check(preservedSize.error.isEmpty() && QImage(preservedSize.path).size() == medium.size() &&
              QFileInfo(preservedSize.path).size() < qualityFifty.size() + 1,
          "quality reduction preserves original dimensions when the budget permits");
    const auto resized = ScreenshotWriter::save(mediumSource, temp.filePath("resized.png"), 4096);
    const QImage reduced(resized.path);
    check(resized.error.isEmpty() && !reduced.isNull() && reduced.width() < medium.width() &&
              reduced.height() < medium.height() && QFileInfo(resized.path).size() < 4096 &&
              qAbs(double(reduced.width()) / reduced.height() - 16.0 / 9) < 0.03,
          "small injected budget forces proportional downscale below strict byte limit");
    const auto impossible = ScreenshotWriter::save(mediumSource, temp.filePath("impossible.png"), 1);
    check(impossible.path.isEmpty() && !impossible.error.isEmpty() &&
              !QFile::exists(temp.filePath("impossible.jpg")),
          "impossible byte budget fails without publishing output");

    QFile damaged(temp.filePath("damaged.png"));
    check(damaged.open(QIODevice::WriteOnly) && damaged.write("broken image") == 12,
          "write damaged input fixture");
    damaged.close();
    const auto corrupt = ScreenshotWriter::save(damaged.fileName(), temp.filePath("damaged-result.png"));
    check(corrupt.path.isEmpty() && !corrupt.error.isEmpty() &&
              !QFile::exists(temp.filePath("damaged-result.png")),
          "corrupt small input is rejected before publishing");
    const auto missing = ScreenshotWriter::save(temp.filePath("missing.png"), temp.filePath("absent.png"));
    check(missing.path.isEmpty() && !missing.error.isEmpty(), "missing source reports file/reader error");
    const auto badDirectory = ScreenshotWriter::save(mediumSource, temp.filePath("missing/output.png"));
    check(badDirectory.path.isEmpty() && !badDirectory.error.isEmpty(),
          "invalid output directory reports staging file error");
    const auto before = read(retained.path);
    const auto duplicatePng = ScreenshotWriter::save(mediumSource, retained.path);
    check(duplicatePng.path.isEmpty() && !duplicatePng.error.isEmpty() && read(retained.path) == before,
          "existing PNG is never overwritten");
    const auto beforeJpeg = read(boundary.path);
    const auto duplicateJpeg = ScreenshotWriter::save(smallSource, temp.filePath("boundary.png"));
    check(duplicateJpeg.path.isEmpty() && !duplicateJpeg.error.isEmpty() &&
              read(boundary.path) == beforeJpeg,
          "existing converted JPEG is never overwritten");
    const auto invalidLimit = ScreenshotWriter::save(mediumSource, temp.filePath("invalid-limit.png"), 0);
    check(invalidLimit.path.isEmpty() && !invalidLimit.error.isEmpty(), "nonpositive size budget rejected");
    check(QDir(temp.path()).entryList({".screenshot-*"}, QDir::Files | QDir::Hidden).isEmpty(),
          "successful and failed writes leave no partial staging files");
    return failures;
}
