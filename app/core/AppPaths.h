#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <QString>

// 路径解析(对齐原 AppPaths 语义):
// - 仓库根:自 exe 目录向上(≤4 级)找含 bilibili.cookie.txt 的目录(开发形态);
//   找不到则以 exe 目录兜底(交付形态由用户放置 cookie 文件在 exe 同级)。
// - macOS .app:Cookie 位于 dataDir()，不修改签名包，也不搜索 /Applications。
// - 数据目录:QStandardPaths::AppLocalDataLocation(%LOCALAPPDATA%/shizi/bbhouse-qt)。
class AppPaths {
   public:
    static QString repoRoot();
    static QString cookiePath();        // <repoRoot>/bilibili.cookie.txt
    static QString normalCookiePath();  // <repoRoot>/bilibili.cookie.normal.txt(调试开关)
    static QString dataDir();
    static QString dbPath();      // <dataDir>/bilibili-history.sqlite3
    static QString exportPath();  // <dataDir>/bilibili-history-export.json
};

#endif  // APP_PATHS_H
