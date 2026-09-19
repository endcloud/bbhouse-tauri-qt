#ifndef HISTORY_SCHEDULER_H
#define HISTORY_SCHEDULER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

struct HistoryServiceConfig {
    QString executablePath;
    QString cookiePath;
    QString databasePath;
    QString exportPath;
    QString time = QStringLiteral("01:00");
    QString cycle = QStringLiteral("daily");
    int weekday = 1;
    bool enabled = true;
};

struct HistoryServiceStatus {
    bool registered = false;
    bool enabled = false;
    QString error;
    QString diagnostic;
};

// macOS LaunchAgent / Windows SCM 服务。Windows 修改由原生 helper 经 UAC 执行；查询不提权。
// 阻塞 API 应在工作线程执行。
// 注入执行器/平台/LaunchAgents 目录仅用于隔离测试，不执行实际注册。
class HistoryScheduler {
public:
    enum class Platform { Native, MacOS, Windows, Unsupported };
    struct CommandResult {
        int exitCode = -1;
        QString output;
        QString error;
        bool timedOut = false;
        bool started = true;
    };
    using CommandExecutor = std::function<CommandResult(const QString &, const QStringList &)>;

    explicit HistoryScheduler(QString configPath, CommandExecutor executor = {},
                              Platform platform = Platform::Native,
                              QString launchAgentsDirectory = {}, QString userIdentity = {});
    static HistoryServiceConfig loadConfig(const QString &path);
    static void saveConfig(const QString &path, const HistoryServiceConfig &config);
    static void validateConfig(const HistoryServiceConfig &config);
    static QByteArray launchAgentPlist(const HistoryServiceConfig &config,
                                      const QString &configPath, const QString &label);

    QString configPath() const { return configPath_; }
    QString taskName() const { return taskName_; }
    QString launchAgentPath() const;
    HistoryServiceStatus query();
    void install(const HistoryServiceConfig &config);
    void update(const HistoryServiceConfig &config);
    void setEnabled(bool enabled);
    void uninstall();

private:
    CommandResult command(const QStringList &args);
    void checked(const QStringList &args);
    QString domain() const;
    void apply(const HistoryServiceConfig &config);
    void applyMac(const HistoryServiceConfig &config);
    void mutateWindows(const HistoryServiceConfig &config, bool requireRegistered);
    void mutate(const HistoryServiceConfig &config, bool requireRegistered);
    void removeTask();

    QString configPath_;
    QString taskName_;
    QString launchAgentsDirectory_;
    QString userIdentity_;
    CommandExecutor executor_;
    Platform platform_;
};

#endif
