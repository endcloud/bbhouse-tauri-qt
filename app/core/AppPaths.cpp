#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

QString AppPaths::repoRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    // Signed application bundles are read-only program assets. Keep credentials
    // in the existing user data directory; never ask users to edit Contents/.
    QDir bundle = dir;
    if (bundle.dirName() == "MacOS" && bundle.cdUp() && bundle.dirName() == "Contents"
        && bundle.cdUp() && bundle.dirName().endsWith(".app"))
        return dataDir();
#endif
    for (int i = 0; i < 4 && dir.exists(); ++i) {
        if (dir.exists("bilibili.cookie.txt")) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QCoreApplication::applicationDirPath();
}

QString AppPaths::cookiePath() { return repoRoot() + "/bilibili.cookie.txt"; }

QString AppPaths::normalCookiePath() {
    return repoRoot() + "/bilibili.cookie.normal.txt";
}

QString AppPaths::dataDir() {
    QString path = qEnvironmentVariable("BBHOUSE_DATA_DIR");
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(path);
    return path;
}

QString AppPaths::dbPath() { return dataDir() + "/bilibili-history.sqlite3"; }

QString AppPaths::exportPath() {
    return dataDir() + "/bilibili-history-export.json";
}
