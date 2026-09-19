#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QHash>
#include <QSet>
#include <atomic>
#include <memory>
#include "downloads/DownloadStore.h"
#include "downloads/DownloadWorker.h"

class QSettings;
class DownloadController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList downloading READ downloading NOTIFY recordsChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY recordsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString downloadDirectory READ downloadDirectory WRITE setDownloadDirectory NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadVideo READ downloadVideo WRITE setDownloadVideo NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadAudio READ downloadAudio WRITE setDownloadAudio NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadDanmaku READ downloadDanmaku WRITE setDownloadDanmaku NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadSubtitles READ downloadSubtitles WRITE setDownloadSubtitles NOTIFY settingsChanged)
    Q_PROPERTY(int preferredQn READ preferredQn WRITE setPreferredQn NOTIFY settingsChanged)
    Q_PROPERTY(QString toolStatus READ toolStatus NOTIFY settingsChanged)
    Q_PROPERTY(QString aria2Path READ aria2Path WRITE setAria2Path NOTIFY settingsChanged)
    Q_PROPERTY(QString ffmpegPath READ ffmpegPath WRITE setFfmpegPath NOTIFY settingsChanged)
public:
    using Runner = std::function<QVariantMap(QVariantMap, const std::shared_ptr<std::atomic_bool> &,
                                           const DownloadWorker::Progress &, const QNetworkProxy &)>;
    explicit DownloadController(QObject *parent = nullptr);
    // All storage is isolated when supplied; no tests need the user's settings/database.
    explicit DownloadController(const QString &dataDirectory, QObject *parent = nullptr, Runner runner = {});
    ~DownloadController() override;
    QVariantList downloading() const;
    QVariantList library() const;
    QString error() const { return error_; }
    QString statusMessage() const { return statusMessage_; }
    QString downloadDirectory() const;
    bool downloadVideo() const;
    bool downloadAudio() const;
    bool downloadDanmaku() const;
    bool downloadSubtitles() const;
    int preferredQn() const;
    QString toolStatus() const;
    QString aria2Path() const;
    QString ffmpegPath() const;
    void setAria2Path(const QString &path);
    void setFfmpegPath(const QString &path);
    void setDownloadDirectory(const QString &path);
    void setDownloadVideo(bool enabled);
    void setDownloadAudio(bool enabled);
    void setDownloadDanmaku(bool enabled);
    void setDownloadSubtitles(bool enabled);
    void setPreferredQn(int qn);
    Q_INVOKABLE void enqueue(const QVariantMap &entry);
    Q_INVOKABLE void requestDownload(const QVariantMap &entry);
    Q_INVOKABLE void retry(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void importFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void removeRecord(const QString &id, bool deleteFiles = false);
    Q_INVOKABLE QVariantMap localEntry(const QString &id);
    Q_INVOKABLE void clearError();
    void shutdown();
    Q_INVOKABLE void refreshTools() { emit settingsChanged(); }
signals:
    void recordsChanged();
    void errorChanged();
    void statusMessageChanged();
    void settingsChanged();
    void downloadRequested(const QVariantMap &entry);
    void taskProgress(const QString &id, const QString &message, int progress);
private:
    void setSetting(const QString &key, const QVariant &value);
    void setError(const QString &error);
    void setStatus(const QString &status);
    bool saveRecord(const QVariantMap &record);
    void startNext();
    void updateProgress(const QString &id, const QString &status, int progress);
    void extractCover(const QVariantMap &record);
    void finishRemoval(const QVariantMap &record, bool deleteFiles);
    void persistRecord(const QVariantMap &record);
    DownloadStore store_;
    std::unique_ptr<QSettings> settings_;
    QVariantList records_;
    QString error_, statusMessage_, activeId_;
    std::shared_ptr<std::atomic_bool> canceled_;
    QFutureWatcher<QVariantMap> watcher_;
    QString coverDirectory_;
    QThreadPool storagePool_;
    QThreadPool coverPool_;
    QHash<QString, std::shared_ptr<std::atomic_bool>> coverJobs_;
    QHash<QString, QFuture<QVariantMap>> coverFutures_;
    QHash<QString, bool> removals_;
    QSet<QString> deletingPaths_;
    Runner runner_;
    bool storeReady_ = false;
    bool stopping_ = false;
};
