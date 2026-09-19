#include "downloads/DownloadController.h"
#include "downloads/DownloadUtils.h"
#include "downloads/DownloadWorker.h"
#include "downloads/DownloadCover.h"
#include "core/AppPaths.h"
#include "core/ApiErrors.h"
#include "core/PlaybackEntry.h"
#include "preferences/AppPreferences.h"
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <QtConcurrent>

namespace {
QString normalizedPath(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return {};
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    QString result = canonical.isEmpty() ? file.absoluteFilePath() : canonical;
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return QDir::cleanPath(result);
}
QStringList recordFiles(const QVariantMap &record) {
    QStringList paths;
    for (const auto *key : {"localPath", "danmakuPath"}) {
        const QString path = record.value(key).toString();
        if (!path.isEmpty()) paths << path;
    }
    paths += record.value("subtitlePaths").toStringList();
    const QString media = record.value("localPath").toString();
    if (!media.isEmpty()) {
        const QFileInfo info(media);
        for (const auto *suffix : {".xml", ".srt"}) {
            const QString sibling = info.dir().filePath(info.completeBaseName() + suffix);
            if (QFileInfo::exists(sibling)) paths << sibling;
        }
    }
    const QString base = record.value("basePath").toString();
    if (!base.isEmpty() && record.value("state") != "imported") {
        for (const auto *stream : {".video.m4s", ".audio.m4s"})
            for (const auto *suffix : {"", ".complete", ".aria2"}) paths << base + stream + suffix;
    }
    paths.removeDuplicates();
    return paths;
}
bool removeFiles(const QStringList &paths, const QSet<QString> &protectedPaths) {
    // Exact record files only. Never recursively clear a directory or follow
    // a symlink. Preflight shared files before deleting any of this record.
    for (const auto &path : paths) {
        const QFileInfo file(path);
        if (!file.exists() && !file.isSymLink()) continue;
        if (!file.isAbsolute() || !file.isFile() || file.isSymLink()
            || protectedPaths.contains(normalizedPath(path))) return false;
    }
    for (const auto &path : paths) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
    }
    return true;
}
bool removeEmptyDownloadDirectory(const QVariantMap &record) {
    if (record.value("state") == "imported" || record.value("business") == "local") return true;
    const QFileInfo base(record.value("basePath").toString());
    if (!base.isAbsolute() || base.fileName().isEmpty()) return true;
    const QFileInfo directory(base.absolutePath());
    // reserveBase creates <title_timestamp>/<title_timestamp>. Remove only
    // that exact owned namespace, never the configured downloads root.
    static const QRegularExpression suffix(QStringLiteral("_\\d{8}_\\d{6}_\\d{3}$"));
    if (directory.fileName() != base.fileName() || !suffix.match(base.fileName()).hasMatch()
        || directory.isSymLink() || !directory.exists()) return true;
    const QDir folder(directory.absoluteFilePath());
    if (!folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
        return true; // Preserve any unrelated files, including hidden files.
    return QDir().rmdir(directory.absoluteFilePath()); // One empty directory; no parent traversal.
}
}

DownloadController::DownloadController(QObject *parent)
    : DownloadController(AppPaths::dataDir(), parent) {}
DownloadController::DownloadController(const QString &dataDirectory, QObject *parent, Runner runner)
    : QObject(parent), store_(QDir(dataDirectory).filePath("downloads.sqlite")),
      settings_(std::make_unique<QSettings>(QDir(dataDirectory).filePath("downloads.ini"), QSettings::IniFormat)) {
    runner_ = runner ? std::move(runner) : DownloadWorker::run;
    coverDirectory_ = QDir(dataDirectory).filePath("download-covers");
    storagePool_.setMaxThreadCount(1); // Preserve save/remove ordering without blocking the GUI.
    coverPool_.setMaxThreadCount(1);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &DownloadController::shutdown);
    try {
        store_.initialize();
        records_ = store_.load();
        for (auto &value : records_) {
            auto record = value.toMap();
            const QString state = record.value("state").toString();
            if (state == "queued" || state == "running") {
                record["state"] = "interrupted";
                record["message"] = Loc::get("上次退出时下载未完成，可重试");
                store_.save(record); value = record;
            }
        }
        storeReady_ = true;
    } catch (const std::exception &) {
        setError(Loc::get("无法打开下载记录数据库"));
    }
    connect(&watcher_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        auto result = watcher_.result();
        const QString id = activeId_;
        activeId_.clear(); canceled_.reset();
        if (removals_.contains(id)) {
            finishRemoval(result, removals_.value(id));
            startNext();
            return;
        }
        saveRecord(result);
        setStatus(result.value("message").toString());
        if (result.value("state") == "failed") setError(result.value("message").toString());
        else if (!result.value("cleanupWarning").toString().isEmpty())
            setError(result.value("cleanupWarning").toString());
        refreshLibrary();
        startNext();
    });
    refreshLibrary();
    QTimer::singleShot(0, this, [this] {
        for (const auto &record : records_) extractCover(record.toMap());
    });
}
DownloadController::~DownloadController() {
    shutdown();
    watcher_.waitForFinished();
    // The finished signal may not have run when shutdown blocks the event loop.
    if (!activeId_.isEmpty() && watcher_.isFinished()) {
        if (removals_.contains(activeId_)) finishRemoval(watcher_.result(), removals_.value(activeId_));
        else persistRecord(watcher_.result());
    }
    coverPool_.waitForDone();
    storagePool_.waitForDone();
}
void DownloadController::shutdown() {
    stopping_ = true;
    if (canceled_) canceled_->store(true);
    for (const auto &flag : coverJobs_) flag->store(true);
}
QVariantList DownloadController::downloading() const {
    QVariantList result;
    for (const auto &value : records_) {
        const auto record = value.toMap();
        if (record.value("state") != "completed" && record.value("state") != "imported")
            result.append(record);
    }
    return result;
}
QVariantList DownloadController::library() const {
    QVariantList result;
    for (const auto &value : records_) {
        const auto record = value.toMap();
        if (record.value("state") == "completed" || record.value("state") == "imported")
            result.append(record);
    }
    return result;
}
QString DownloadController::downloadDirectory() const {
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (fallback.isEmpty()) fallback = QDir::home().filePath("Downloads");
    return settings_->value("download/directory", fallback).toString();
}
bool DownloadController::downloadVideo() const { return settings_->value("download/video", true).toBool(); }
bool DownloadController::downloadAudio() const {
    // Legacy preferences may select both. The video mode now includes audio.
    return !downloadVideo() && settings_->value("download/audio", true).toBool();
}
bool DownloadController::downloadDanmaku() const { return settings_->value("download/danmaku", true).toBool(); }
bool DownloadController::downloadSubtitles() const { return settings_->value("download/subtitles", true).toBool(); }
int DownloadController::preferredQn() const { return settings_->value("download/qn", 80).toInt(); }
QString DownloadController::aria2Path() const { return settings_->value("download/aria2Path").toString(); }
QString DownloadController::ffmpegPath() const { return settings_->value("download/ffmpegPath").toString(); }
void DownloadController::setAria2Path(const QString &path) { setSetting("download/aria2Path", path.trimmed()); }
void DownloadController::setFfmpegPath(const QString &path) { setSetting("download/ffmpegPath", path.trimmed()); }
QString DownloadController::toolStatus() const {
    QStringList missing;
    for (const auto &tool : {"aria2c", "ffmpeg", "curl"}) {
        const QString custom = QString(tool) == "aria2c" ? aria2Path() : QString(tool) == "ffmpeg" ? ffmpegPath() : QString();
        if (custom.isEmpty() ? DownloadUtils::findTool(tool).isEmpty()
                             : !QFileInfo(custom).isExecutable()) missing.append(tool);
    }
    return missing.isEmpty() ? Loc::get("aria2、FFmpeg、curl 已就绪")
        : Loc::get("缺少下载工具：%1。请安装后刷新检测，或放入程序 tools 目录").arg(missing.join(", "));
}
void DownloadController::setSetting(const QString &key, const QVariant &value) {
    if (settings_->value(key) == value) return;
    settings_->setValue(key, value); settings_->sync();
    emit settingsChanged();
}
void DownloadController::setDownloadDirectory(const QString &input) {
    const QUrl url(input);
    const QString path = QDir::cleanPath(url.isLocalFile() ? url.toLocalFile() : input.trimmed());
    if (path.isEmpty() || !QFileInfo(path).isAbsolute() || path.contains('\n') || path.contains('\r')) {
        setError(Loc::get("请选择有效的本地下载目录")); return;
    }
    setSetting("download/directory", path);
}
void DownloadController::setDownloadVideo(bool enabled) {
    if (enabled || downloadVideo()) settings_->setValue("download/audio", false);
    settings_->setValue("download/video", enabled);
    settings_->sync();
    emit settingsChanged();
}
void DownloadController::setDownloadAudio(bool enabled) {
    if (enabled) settings_->setValue("download/video", false);
    // Always notify: legacy audio=true can already be stored while video was selected.
    settings_->setValue("download/audio", enabled);
    settings_->sync();
    emit settingsChanged();
}
void DownloadController::setDownloadDanmaku(bool enabled) { setSetting("download/danmaku", enabled); }
void DownloadController::setDownloadSubtitles(bool enabled) { setSetting("download/subtitles", enabled); }
void DownloadController::setPreferredQn(int qn) {
    if (QList<int>{16, 32, 64, 74, 80, 112, 116, 120, 125, 126, 127}.contains(qn))
        setSetting("download/qn", qn);
}
void DownloadController::setError(const QString &message) { if (error_ != message) { error_ = message; emit errorChanged(); } }
void DownloadController::setStatus(const QString &message) { if (statusMessage_ != message) { statusMessage_ = message; emit statusMessageChanged(); } }
void DownloadController::clearError() { setError({}); }
void DownloadController::requestDownload(const QVariantMap &input) {
    const auto entry = PlaybackEntry::normalize(input);
    if (!QStringList{"archive", "pgc"}.contains(entry.value("business").toString())
        || !PlaybackEntry::playable(entry)) { setError(Loc::get("此条目不支持下载")); return; }
    emit downloadRequested(entry);
}
bool DownloadController::saveRecord(const QVariantMap &record) {
    if (!storeReady_) { setError(Loc::get("保存下载记录失败，请检查数据目录")); return false; }
    persistRecord(record);
    bool found = false;
    for (auto &value : records_) {
        if (value.toMap().value("id") == record.value("id")) { value = record; found = true; break; }
    }
    if (!found) records_.prepend(record);
    emit recordsChanged(); return true;
}
void DownloadController::persistRecord(const QVariantMap &record) {
    auto *pending = new QFutureWatcher<bool>(this);
    connect(pending, &QFutureWatcher<bool>::finished, this, [this, pending] {
        if (!pending->result()) setError(Loc::get("保存下载记录失败，请检查数据目录"));
        pending->deleteLater();
    });
    const DownloadStore store = store_;
    pending->setFuture(QtConcurrent::run(&storagePool_, [store, record] {
        try { store.save(record); return true; } catch (...) { return false; }
    }));
}
void DownloadController::enqueue(const QVariantMap &input) {
    clearError();
    const auto entry = PlaybackEntry::normalize(input);
    if (!QStringList{"archive", "pgc"}.contains(entry.value("business").toString())
        || !PlaybackEntry::playable(entry)) { setError(Loc::get("此条目不支持下载")); return; }
    const auto options = input.value("downloadOptions").toMap();
    const bool video = options.value("video", options.value("downloadVideo", downloadVideo())).toBool();
    const bool audio = options.value("audio", options.value("downloadAudio", downloadAudio())).toBool();
    const bool danmaku = options.value("danmaku", options.value("downloadDanmaku", downloadDanmaku())).toBool();
    const bool subtitles = options.value("subtitles", options.value("downloadSubtitles", downloadSubtitles())).toBool();
    const int qn = options.value("preferredQn", options.value("qn", preferredQn())).toInt();
    if (!video && !audio && !danmaku && !subtitles) {
        setError(Loc::get("请至少选择一种下载内容")); return;
    }
    QStringList needed;
    if (video || audio) needed << "aria2c" << "ffmpeg";
    if (danmaku || subtitles) needed << "curl";
    for (const auto &tool : needed) {
        const QString custom = tool == "aria2c" ? aria2Path() : tool == "ffmpeg" ? ffmpegPath() : QString();
        if (custom.isEmpty() ? DownloadUtils::findTool(tool).isEmpty()
                            : !QFileInfo(custom).isExecutable()) { setError(toolStatus()); return; }
    }
    QVariantMap record = DownloadUtils::safeMetadata(entry);
    record["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record["state"] = "queued"; record["progress"] = 0;
    record["message"] = Loc::get("等待下载");
    record["downloadVideo"] = video; record["downloadAudio"] = video || audio;
    record["downloadDanmaku"] = danmaku; record["downloadSubtitles"] = subtitles;
    record["preferredQn"] = qn;
    try { record["basePath"] = DownloadUtils::reserveBase(downloadDirectory(), record.value("title").toString()); }
    catch (...) { setError(Loc::get("无法创建下载目录，请检查路径和写入权限")); return; }
    if (saveRecord(record)) { setStatus(Loc::get("已加入下载队列")); startNext(); }
}
void DownloadController::startNext() {
    if (stopping_ || !activeId_.isEmpty() || watcher_.isRunning()) return;
    for (const auto &value : records_) {
        auto record = value.toMap();
        if (record.value("state") != "queued") continue;
        const QString id = record.value("id").toString();
        if (removals_.contains(id)) continue;
        record["state"] = "running";
        if (!saveRecord(record)) return;
        activeId_ = id; canceled_ = std::make_shared<std::atomic_bool>(false);
        record["aria2Path"] = aria2Path(); record["ffmpegPath"] = ffmpegPath();
        const auto flag = canceled_;
        const auto runner = runner_;
        const QNetworkProxy proxy = record.value("regionalApiProxy").toBool()
            ? AppPreferences::instance()->regionalProxy() : QNetworkProxy(QNetworkProxy::NoProxy);
        watcher_.setFuture(QtConcurrent::run([this, record, flag, id, proxy, runner] {
            return runner(record, flag, [this, id](const QString &stage, int progress) {
                QMetaObject::invokeMethod(this, [this, id, stage, progress] {
                    if (!stopping_) updateProgress(id, stage, progress);
                }, Qt::QueuedConnection);
            }, proxy);
        }));
        return;
    }
}
void DownloadController::updateProgress(const QString &id, const QString &stage, int progress) {
    if (activeId_ != id || removals_.contains(id)) return;
    for (auto &value : records_) {
        auto record = value.toMap();
        if (record.value("id").toString() != id) continue;
        if (record.value("message").toString() == stage && record.value("progress").toInt() == progress) return;
        record["message"] = stage; record["progress"] = progress;
        value = record;
        // No SQL, file checks or whole-list notification on the progress path.
        emit taskProgress(id, stage, progress);
        return;
    }
}
void DownloadController::retry(const QString &id) {
    if (removals_.contains(id)) return;
    for (const auto &value : records_) {
        auto record = value.toMap();
        if (record.value("id").toString() != id) continue;
        if (record.value("state") != "failed" && record.value("state") != "canceled"
            && record.value("state") != "interrupted") return;
        record["state"] = "queued"; record["message"] = Loc::get("等待重试"); record["progress"] = 0;
        if (saveRecord(record)) startNext();
        return;
    }
}
void DownloadController::cancel(const QString &id) {
    if (activeId_ == id && canceled_) { canceled_->store(true); return; }
    for (const auto &value : records_) {
        auto record = value.toMap();
        if (record.value("id").toString() != id || record.value("state") != "queued") continue;
        record["state"] = "canceled"; record["message"] = Loc::get("已取消，已下载文件保留，可重试");
        saveRecord(record); return;
    }
}
void DownloadController::refreshLibrary() {
    for (auto &value : records_) {
        auto record = value.toMap();
        const QString path = record.value("localPath").toString();
        record["available"] = !path.isEmpty() && DownloadUtils::readableMedia(path);
        record["playable"] = record.value("available");
        if (record.value("state") == "imported") record["danmakuPath"] = DownloadUtils::siblingDanmaku(path);
        if (!path.isEmpty() && !record.value("available").toBool()) record["availabilityMessage"] = Loc::get("本地媒体不存在或不可读");
        else if (path.isEmpty()) record["availabilityMessage"] = Loc::get("仅附件，无可播放媒体");
        else record["availabilityMessage"] = QString();
        value = record;
    }
    emit recordsChanged();
}
void DownloadController::importFiles(const QList<QUrl> &urls) {
    clearError(); int imported = 0;
    for (const auto &url : urls) {
        if (!url.isLocalFile()) { setError(Loc::get("仅支持导入本地媒体文件")); continue; }
        const QFileInfo file(url.toLocalFile());
        const QString path = file.canonicalFilePath();
        if (deletingPaths_.contains(normalizedPath(path))) {
            setError(Loc::get("该文件正在移除，请稍后再试")); continue;
        }
        if (!QStringList{"mp4", "mkv", "flv", "webm", "mov", "avi", "m4v", "ts", "m4a", "mp3", "aac", "flac", "wav", "ogg", "opus", "mka"}.contains(file.suffix().toLower())
            || !DownloadUtils::readableMedia(path)) {
            setError(Loc::get("导入失败：请选择存在且可读的视频或音频文件")); continue;
        }
        bool duplicate = false;
        for (const auto &value : records_)
            if (QFileInfo(value.toMap().value("localPath").toString()).canonicalFilePath() == path) { duplicate = true; break; }
        if (duplicate) continue;
        QVariantMap record{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"title", file.completeBaseName()}, {"business", "local"}, {"state", "imported"},
            {"progress", 100}, {"localPath", path}, {"danmakuPath", DownloadUtils::siblingDanmaku(path)},
            {"message", Loc::get("本地导入")}};
        const QString srt = file.dir().filePath(file.completeBaseName() + ".srt");
        if (DownloadUtils::readableMedia(srt)) record["subtitlePaths"] = QStringList{srt};
        if (saveRecord(record)) { ++imported; extractCover(record); }
    }
    refreshLibrary(); setStatus(Loc::get("已导入 %1 个媒体文件").arg(imported));
}
QVariantMap DownloadController::localEntry(const QString &id) {
    if (removals_.contains(id)) { setError(Loc::get("该记录正在移除")); return {}; }
    refreshLibrary();
    for (const auto &value : records_) {
        auto record = value.toMap();
        if (record.value("id").toString() != id) continue;
        if (!record.value("available").toBool()) { setError(record.value("availabilityMessage").toString()); return {}; }
        record["business"] = "local";
        record["videoKey"] = "local:" + id;
        // Check on every play, including sidecars added after import/download.
        const QString sibling = DownloadUtils::siblingDanmaku(record.value("localPath").toString());
        if (!sibling.isEmpty()) record["danmakuPath"] = sibling;
        else if (!DownloadUtils::readableMedia(record.value("danmakuPath").toString())) record["danmakuPath"] = QString();
        const auto subtitles = record.value("subtitlePaths").toStringList();
        for (const auto &path : subtitles) if (DownloadUtils::readableMedia(path)) { record["subtitlePath"] = path; break; }
        return record;
    }
    setError(Loc::get("未找到本地媒体记录")); return {};
}

void DownloadController::removeRecord(const QString &id, bool deleteFiles) {
    if (stopping_ || removals_.contains(id)) return;
    for (const auto &value : records_) {
        const auto record = value.toMap();
        if (record.value("id").toString() != id) continue;
        clearError();
        removals_.insert(id, deleteFiles);
        if (coverJobs_.contains(id)) coverJobs_.value(id)->store(true);
        setStatus(Loc::get("正在移除记录"));
        if (id == activeId_ && canceled_) {
            canceled_->store(true); // Wait for the process to exit before deleting its files.
        } else finishRemoval(record, deleteFiles);
        return;
    }
}

void DownloadController::extractCover(const QVariantMap &record) {
    if (stopping_ || record.value("state") != "imported") return;
    const QString id = record.value("id").toString();
    if (coverJobs_.contains(id) || removals_.contains(id)) return;
    auto flag = std::make_shared<std::atomic_bool>(false);
    coverJobs_.insert(id, flag);
    auto *pending = new QFutureWatcher<QVariantMap>(this);
    connect(pending, &QFutureWatcher<QVariantMap>::finished, this, [this, pending, id] {
        const auto cover = pending->result();
        pending->deleteLater();
        coverJobs_.remove(id);
        coverFutures_.remove(id);
        if (stopping_ || removals_.contains(id) || cover.isEmpty()) return;
        // Merge into the latest record, never reinsert a removed snapshot.
        for (const auto &value : records_) {
            auto current = value.toMap();
            if (current.value("id").toString() != id) continue;
            if (current.value("coverUrl") == cover.value("coverUrl")) return;
            for (auto it = cover.begin(); it != cover.end(); ++it) current.insert(it.key(), it.value());
            saveRecord(current);
            return;
        }
    });
    const QString media = record.value("localPath").toString();
    const QString directory = coverDirectory_;
    const QString tool = ffmpegPath();
    pending->setFuture(QtConcurrent::run(&coverPool_, [media, directory, flag, tool] {
        return DownloadCover::extract(media, directory, flag, tool);
    }));
    coverFutures_.insert(id, pending->future());
}

void DownloadController::finishRemoval(const QVariantMap &record, bool deleteFiles) {
    const QString id = record.value("id").toString();
    const QStringList paths = deleteFiles ? recordFiles(record) : QStringList();
    QSet<QString> protectedPaths;
    for (const auto &value : records_) {
        const auto other = value.toMap();
        if (other.value("id").toString() == id) continue;
        for (const auto &path : recordFiles(other)) protectedPaths.insert(normalizedPath(path));
    }
    for (const auto &path : paths) deletingPaths_.insert(normalizedPath(path));
    auto *pending = new QFutureWatcher<bool>(this);
    connect(pending, &QFutureWatcher<bool>::finished, this, [this, pending, record, id, paths] {
        const bool removed = pending->result();
        pending->deleteLater();
        removals_.remove(id);
        for (const auto &path : paths) deletingPaths_.remove(normalizedPath(path));
        if (removed) {
            for (qsizetype i = records_.size(); i-- > 0;)
                if (records_.at(i).toMap().value("id").toString() == id) records_.removeAt(i);
            emit recordsChanged();
            setStatus(Loc::get("已移除记录"));
        } else {
            saveRecord(record);
            setError(Loc::get("移除失败：请检查文件权限，或确认文件未被其他记录使用"));
            refreshLibrary();
        }
        startNext();
    });
    const DownloadStore store = store_;
    auto coverFuture = coverFutures_.value(id);
    pending->setFuture(QtConcurrent::run(&storagePool_, [store, id, record, deleteFiles, paths, protectedPaths, coverFuture]() mutable {
        // On Windows the canceled FFmpeg process must release its source-file
        // handle before physical removal. Wait here, never on the GUI thread.
        if (coverFuture.isStarted()) coverFuture.waitForFinished();
        if (!removeFiles(paths, protectedPaths)) return false;
        if (deleteFiles && !removeEmptyDownloadDirectory(record)) return false;
        try { store.remove(id); return true; } catch (...) { return false; }
    }));
}
