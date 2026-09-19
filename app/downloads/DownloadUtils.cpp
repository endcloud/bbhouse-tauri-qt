#include "downloads/DownloadUtils.h"
#include "core/ApiErrors.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>
#include <cmath>
#ifdef Q_OS_WIN
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

namespace DownloadUtils {
QString safeName(QString value) {
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), "_");
    value = value.trimmed().left(100);
    while (value.endsWith('.') || value.endsWith(' ')) value.chop(1);
    if (value.isEmpty() || value == "." || value == "..") value = "video";
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                           QRegularExpression::CaseInsensitiveOption).match(value).hasMatch())
        value.prepend('_');
    return value;
}

QString findTool(const QString &name) {
    QString executable = name;
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    const QString app = QCoreApplication::applicationDirPath();
    QStringList paths{app + "/tools", app, app + "/../Resources/tools"};
#ifdef Q_OS_MACOS
    paths << "/opt/homebrew/bin" << "/usr/local/bin" << "/usr/bin";
#endif
    const QString bundled = QStandardPaths::findExecutable(executable, paths);
    return bundled.isEmpty() ? QStandardPaths::findExecutable(executable) : bundled;
}

static QString srtTime(double seconds) {
    const qint64 ms = qRound64(seconds * 1000);
    return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

QString subtitleToSrt(const QByteArray &json) {
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value("body").isArray())
        throw std::runtime_error("Invalid subtitle JSON");
    QString result;
    int index = 0;
    for (const auto &value : document.object().value("body").toArray()) {
        const auto row = value.toObject();
        const double from = row.value("from").toDouble(-1);
        const double to = row.value("to").toDouble(-1);
        QString content = row.value("content").toString();
        content.replace("\r\n", "\n");
        content.replace('\r', '\n');
        if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to <= from
            || to > 360000000 || content.trimmed().isEmpty()) continue;
        result += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
            .arg(++index).arg(srtTime(from), srtTime(to), content.trimmed());
    }
    return result;
}

static QByteArray inflateBytes(const QByteArray &input, int bits) {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream.avail_in = input.size();
    if (inflateInit2(&stream, bits) != Z_OK) return {};
    QByteArray output;
    char buffer[32768];
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer, sizeof(buffer) - stream.avail_out);
        if (output.size() > 64 * 1024 * 1024) { status = Z_MEM_ERROR; break; }
    } while (status == Z_OK);
    inflateEnd(&stream);
    return status == Z_STREAM_END ? output : QByteArray();
}

QByteArray danmakuXml(const QByteArray &bytes) {
    QByteArray xml = bytes;
    if (!xml.trimmed().startsWith('<')) {
        xml = inflateBytes(bytes, MAX_WBITS + 32);
        if (xml.isEmpty()) xml = inflateBytes(bytes, -MAX_WBITS);
    }
    QXmlStreamReader reader(xml);
    if (!reader.readNextStartElement() || reader.name() != QLatin1String("i"))
        throw std::runtime_error("Invalid danmaku XML");
    while (!reader.atEnd()) reader.readNext();
    if (reader.hasError()) throw std::runtime_error("Invalid danmaku XML");
    return xml;
}

bool readableMedia(const QString &path) {
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.isFile() || info.size() <= 0 || !info.isReadable()) return false;
    QFile file(path);
    return file.open(QIODevice::ReadOnly);
}

QString siblingDanmaku(const QString &path) {
    const QFileInfo media(path);
    const QString xml = media.dir().filePath(media.completeBaseName() + ".xml");
    return readableMedia(xml) ? xml : QString();
}

QVariantMap safeMetadata(const QVariantMap &entry) {
    QVariantMap clean;
    // Deliberate allowlist: never persist rawJson, cookies or signed media URLs.
    for (const char *key : {"title", "authorName", "business", "bvid", "duration", "part", "regionalApiProxy"})
        if (entry.contains(key)) clean.insert(key, entry.value(key));
    for (const char *key : {"oid", "cid", "epId", "authorMid"})
        clean.insert(key, QString::number(entry.value(key).toLongLong()));
    const QUrl cover(entry.value("coverUrl").toString());
    if ((cover.scheme() == "http" || cover.scheme() == "https")
        && (cover.host().endsWith(".hdslb.com") || cover.host() == "hdslb.com")) {
        QUrl publicCover = cover;
        publicCover.setQuery(QString());
        publicCover.setFragment(QString());
        publicCover.setUserInfo(QString());
        clean.insert("coverUrl", publicCover.toString());
    }
    return clean;
}

bool cleanupCompleted(const QVariantMap &record) {
    if (record.value("state").toString() != QLatin1String("completed")) return false;
    const QString base = record.value("basePath").toString();
    const QFileInfo baseInfo(base), media(record.value("localPath").toString());
    if (!baseInfo.isAbsolute() || baseInfo.fileName().isEmpty() || !media.isAbsolute()
        || !readableMedia(media.absoluteFilePath()) || media.isSymLink()
        || (media.suffix() != "mp4" && media.suffix() != "m4a")
        || (media.completeBaseName() != baseInfo.fileName()
            && !media.completeBaseName().startsWith(baseInfo.fileName() + '_'))
        || baseInfo.dir().canonicalPath().isEmpty()
        || baseInfo.dir().canonicalPath() != media.dir().canonicalPath()) return false;

    // Never sweep a directory: exact task-owned names only. Leave final media,
    // sidecars, unrelated files, directories and symlink targets untouched.
    bool cleaned = true;
    for (const auto *stream : {".video.m4s", ".audio.m4s"}) {
        for (const auto *suffix : {"", ".complete", ".aria2"}) {
            const QString path = base + QLatin1String(stream) + QLatin1String(suffix);
            const QFileInfo file(path);
            if (!file.exists() && !file.isSymLink()) continue;
            if (!file.isFile() || file.isSymLink() || !QFile::remove(path)) cleaned = false;
        }
    }
    return cleaned;
}

QString reserveBase(const QString &directory, const QString &title) {
    if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create download directory");
    const QString name = safeName(title);
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    for (int attempt = 0; attempt < 10000; ++attempt, ++timestamp) {
        const QString unique = name + '_' + QDateTime::fromMSecsSinceEpoch(timestamp)
            .toString("yyyyMMdd_HHmmss_zzz");
        const QString folder = QDir(directory).filePath(unique);
        // mkdir atomically reserves the namespace without replacing user files.
        if (QDir().mkdir(folder)) return QDir(folder).filePath(unique);
    }
    throw std::runtime_error("Cannot reserve download filename");
}
}
