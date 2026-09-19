#include "core/HistoryScheduler.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QDebug>
#include <functional>

namespace {
using Scheduler = HistoryScheduler;
using Result = Scheduler::CommandResult;
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
QString throws(const std::function<void()> &action) {
    try { action(); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    return {};
}
QStringList stringsIn(const QByteArray &xml) {
    QXmlStreamReader r(xml); QStringList result;
    while (!r.atEnd()) { r.readNext(); if (r.isStartElement() && r.name() == "string") result << r.readElementText(); }
    return result;
}
bool validXml(const QByteArray &xml) { QXmlStreamReader r(xml); while (!r.atEnd()) r.readNext(); return !r.hasError(); }
struct MacFake {
    bool loaded = false, disabled = false;
    int lastExit = 0;
    QString label;
    QString failVerb;
    int failuresLeft = 0;
    bool timeout = false;
    QStringList calls;
    Result run(const QString &program, const QStringList &args) {
        calls << program + " " + args.join('|');
        const QString verb = args.value(0);
        if (verb == failVerb && failuresLeft > 0) {
            --failuresLeft;
            return {13, {}, "permission denied", timeout};
        }
        if (verb == "print") return loaded ? Result{0, "state = waiting\nlast exit code = " + QString::number(lastExit), {}} : Result{113, {}, "not found"};
        if (verb == "print-disabled") return {0, "disabled services = {\n\"" + label + "\" => " + (disabled ? "true" : "false") + "\n}", {}};
        if (verb == "bootstrap") { loaded = true; return {0, {}, {}}; }
        if (verb == "bootout") { loaded = false; return {0, {}, {}}; }
        if (verb == "enable") { disabled = false; return {0, {}, {}}; }
        if (verb == "disable") { disabled = true; return {0, {}, {}}; }
        return {1, {}, "unexpected command"};
    }
};
struct WinFake {
    bool registered = false, enabled = false, running = false;
    int exitCode = 0;
    QString failVerb;
    int failureCode = 5;
    bool malformed = false;
    QStringList calls, stagedPaths;
    QString configPath;
    QStringList lastMutation;
    bool unsafeProgram = false;
    Result run(const QString &program, const QStringList &args) {
        unsafeProgram |= !program.endsWith("bbhouse-history-service.exe");
        calls << args.join('|');
        const QString verb = args.value(0);
        if (verb == failVerb) return {failureCode, {}, "fixture native service failure"};
        if (verb == "--query") {
            if (malformed) return {0, "not-json", {}};
            const QJsonObject state{{"registered", registered}, {"enabled", enabled},
                                    {"running", running}, {"exitCode", exitCode}};
            return {0, QString::fromUtf8(QJsonDocument(state).toJson()), {}};
        }
        if (verb == "--uninstall") { registered = enabled = running = false; return {0, {}, {}}; }
        if (verb == "--install" || verb == "--update") {
            lastMutation = args;
            const QString pending = args.value(args.indexOf("--pending-config") + 1);
            stagedPaths.append(pending);
            // Model the approved native helper publishing the staged configuration.
            const auto config = Scheduler::loadConfig(pending);
            const QString target = args.value(args.indexOf("--config") + 1);
            Scheduler::saveConfig(target, config);
            registered = true;
            enabled = config.enabled;
            running = enabled;
            return {0, {}, {}};
        }
        return {87, {}, "unexpected native service operation"};
    }
};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Always beneath this project's build directory, even when invoked from elsewhere.
    QDir project(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()); project.cdUp();
    if (!project.mkpath("build")) return 1;
    QTemporaryDir dir(project.filePath("build/history-scheduler-test-XXXXXX"));
    if (!dir.isValid()) return 1;
    int failures = 0;
    const auto check = [&](bool value, const char *name) { qInfo() << (value ? "PASS" : "FAIL") << name; failures += !value; };
    HistoryServiceConfig c;
    c.executablePath = dir.filePath("app & 中文 fixture.exe");
    QFile executable(c.executablePath);
    if (!executable.open(QIODevice::WriteOnly)) return 1;
    executable.write("fixture"); executable.close();
    executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    c.cookiePath = dir.filePath("cookie path.txt"); c.databasePath = dir.filePath("history.sqlite3"); c.exportPath = dir.filePath("export.json");
    const QString configPath = dir.filePath("config & 中文 test.json");
    Scheduler::saveConfig(configPath, c);
    const auto roundTrip = Scheduler::loadConfig(configPath);
    check(roundTrip.executablePath == c.executablePath && roundTrip.time == "01:00" && roundTrip.enabled, "configuration round trips special characters and defaults");
    check(!read(configPath).contains("SESSDATA") && !read(configPath).contains("cookieContents"), "configuration contains paths only");
    auto bad = c; bad.time = "25:00";
    check(!throws([&] { Scheduler::saveConfig(configPath, bad); }).isEmpty(), "invalid time rejected before save");
    check(Scheduler::loadConfig(configPath).time == "01:00", "invalid save preserves prior configuration");
    bad = c; bad.cookiePath = "relative.cookie";
    check(!throws([&] { Scheduler::validateConfig(bad); }).isEmpty(), "relative paths rejected");
    bad = c; bad.weekday = 0;
    check(!throws([&] { Scheduler::validateConfig(bad); }).isEmpty(), "invalid weekday rejected");
    bad = c; bad.cycle = "hourly";
    check(!throws([&] { Scheduler::validateConfig(bad); }).isEmpty(), "unsupported cycle rejected");
    check(!throws([&] { Scheduler relative("relative.json"); }).isEmpty(), "relative config path rejected");
    auto escaped = c;
    escaped.executablePath += " <video> \"quoted\"";
    const QString escapedConfigPath = configPath + " \"test\"";
    const auto plist = Scheduler::launchAgentPlist(escaped, escapedConfigPath, "fixture");
    check(validXml(plist) && stringsIn(plist).contains(escaped.executablePath) && stringsIn(plist).contains(escapedConfigPath), "plist preserves escaped paths as individual arguments");
    check(stringsIn(plist).contains("--history-sync-once") && !plist.contains("Weekday"), "daily plist uses headless entry and no weekly restriction");
    auto weekly = c; weekly.cycle = "weekly"; weekly.weekday = 7; weekly.time = "23:59";
    const auto weeklyPlist = Scheduler::launchAgentPlist(weekly, configPath, "fixture");
    check(weeklyPlist.contains("<key>Weekday</key>\n            <integer>0</integer>") && weeklyPlist.contains("<integer>23</integer>"), "macOS maps ISO Sunday to zero and honors selected time");
    MacFake mac;
    const QString macConfig = dir.filePath("mac-config.json");
    Scheduler scheduler(macConfig, [&](const QString &p, const QStringList &a) { return mac.run(p, a); }, Scheduler::Platform::MacOS, dir.filePath("LaunchAgents"), "501");
    mac.label = scheduler.taskName();
    check(!scheduler.query().registered && scheduler.query().error.isEmpty(), "macOS absent service is distinct from failure");
    mac.failVerb = "print"; mac.failuresLeft = 1;
    check(!scheduler.query().error.isEmpty(), "macOS permission denial is surfaced");
    mac.failVerb = "print"; mac.failuresLeft = 1; mac.timeout = true;
    check(scheduler.query().error.contains("超时"), "macOS timeout is surfaced"); mac.timeout = false;
    scheduler.install(c);
    check(scheduler.query().registered && scheduler.query().enabled && QFile::exists(scheduler.launchAgentPath()), "macOS registration persists plist and loads job");
    mac.lastExit = 3;
    check(!scheduler.query().diagnostic.isEmpty(), "macOS exposes failed process result even without a database audit");
    mac.lastExit = 0;
    mac.loaded = false;
    check(scheduler.query().registered && !scheduler.query().enabled, "macOS persisted but unloaded agent is registered and inactive");
    mac.loaded = true;
    check(!mac.calls.join('\n').contains("sh -c"), "native commands are passed without a shell");
    scheduler.setEnabled(false);
    check(scheduler.query().registered && !scheduler.query().enabled && QFile::exists(scheduler.launchAgentPath()), "disabled macOS task remains registered persistently");
    scheduler.setEnabled(true);
    check(scheduler.query().enabled, "macOS task re-enables and reloads");
    mac.failVerb = "bootstrap"; mac.failuresLeft = 1;
    check(!throws([&] { scheduler.update(weekly); }).isEmpty(), "macOS failed update reports error");
    check(scheduler.query().enabled && Scheduler::loadConfig(macConfig).cycle == "daily", "macOS failed update restores previous configuration and running task");
    mac.failVerb = "bootstrap"; mac.failuresLeft = 2;
    check(throws([&] { scheduler.update(weekly); }).contains("回滚失败"), "macOS rollback failure is explicit");
    mac.failuresLeft = 0; scheduler.install(c);
    mac.failVerb = "bootout"; mac.failuresLeft = 1;
    check(!throws([&] { scheduler.uninstall(); }).isEmpty() && scheduler.query().enabled, "macOS failed uninstall restores enabled native registration");
    scheduler.uninstall();
    check(!scheduler.query().registered && QFile::exists(macConfig), "macOS uninstall retains configuration and removes registration only");
    check(throws([&] { scheduler.uninstall(); }).isEmpty(), "macOS absent uninstall is idempotent");
    check(!throws([&] { scheduler.update(c); }).isEmpty(), "macOS update requires prior registration");
    mac.failVerb = "bootstrap"; mac.failuresLeft = 1;
    check(!throws([&] { scheduler.install(c); }).isEmpty() && !scheduler.query().registered, "failed macOS install removes partial registration");

    WinFake win;
    const QString winConfig = dir.filePath("win-config.json");
    Scheduler ws(winConfig, [&](const QString &p, const QStringList &a) { return win.run(p, a); }, Scheduler::Platform::Windows);
    check(!ws.query().registered && ws.query().error.isEmpty(), "Windows absent SCM service is distinct from query failure");
    win.failVerb = "--query";
    check(!ws.query().error.isEmpty(), "SCM query access denial does not imply unregistered");
    win.failVerb.clear(); win.malformed = true;
    check(!ws.query().error.isEmpty(), "malformed native service status is rejected"); win.malformed = false;
    win.failVerb = "--install"; win.failureCode = 1223;
    check(throws([&] { ws.install(c); }).contains("UAC") && !QFile::exists(winConfig) && !win.registered,
          "cancelled UAC does not publish configuration or register service");
    check(win.calls.filter("--install").size() == 1 && win.calls.filter("--uninstall").isEmpty(),
          "cancelled UAC does not cause a second elevation for rollback");
    win.failVerb.clear();
    ws.install(c);
    check(ws.query().registered && ws.query().enabled && Scheduler::loadConfig(winConfig).time == "01:00",
          "approved native helper publishes config and reports registered enabled SCM service");
    check(win.lastMutation.contains(c.executablePath) && win.lastMutation.contains(c.cookiePath)
        && win.lastMutation.contains(c.databasePath) && win.lastMutation.contains(c.exportPath)
        && win.lastMutation.contains("--runtime-path") && win.lastMutation.contains(ws.taskName()),
          "native helper receives exact worker paths identity and runtime search path without XML");
    const QByteArray original = read(winConfig);
    win.failVerb = "--update"; win.failureCode = 1223;
    check(throws([&] { ws.update(weekly); }).contains("UAC") && read(winConfig) == original && win.enabled,
          "cancelled update preserves running service and original effective schedule");
    win.failureCode = 5;
    check(!throws([&] { ws.update(weekly); }).isEmpty() && read(winConfig) == original,
          "native mutation failure reports error without publishing a staged plan");
    win.failureCode = 0x20000001;
    check(throws([&] { ws.update(weekly); }).contains("回滚失败"),
          "native transaction rollback failure is explicitly reported");
    win.failVerb.clear();
    ws.update(weekly);
    check(Scheduler::loadConfig(winConfig).cycle == "weekly", "approved weekly plan is saved by helper");
    ws.setEnabled(false);
    check(ws.query().registered && !ws.query().enabled && !Scheduler::loadConfig(winConfig).enabled,
          "pause keeps registration and atomically saves disabled configuration");
    ws.setEnabled(true);
    check(ws.query().enabled && Scheduler::loadConfig(winConfig).enabled, "enable restores service and configuration");
    win.running = false;
    check(!ws.query().diagnostic.isEmpty(), "enabled but stopped service is diagnosed");
    win.exitCode = 126;
    check(ws.query().diagnostic.contains("126"), "SCM failed worker or DLL startup code is visible");
    win.running = true; win.exitCode = 0;
    win.failVerb = "--uninstall"; win.failureCode = 1223;
    check(throws([&] { ws.uninstall(); }).contains("UAC") && ws.query().registered,
          "cancelled uninstall leaves native service registered");
    win.failVerb.clear();
    ws.uninstall();
    check(!ws.query().registered && QFile::exists(winConfig), "SCM uninstall preserves configuration and data");
    const int operations = win.calls.size();
    ws.uninstall();
    check(win.calls.size() == operations + 1, "uninstall absent service only queries without elevation");
    check(!throws([&] { ws.update(c); }).isEmpty(), "Windows update requires existing SCM service");
    bool cleaned = !win.stagedPaths.isEmpty();
    for (const auto &path : win.stagedPaths) cleaned = cleaned && !QFile::exists(path);
    check(cleaned && QDir(dir.path()).entryList({"history-service-request-*"}, QDir::Dirs).isEmpty(),
          "staged mutation requests are cleaned after successful failed and cancelled operations");
    check(!win.unsafeProgram && !win.calls.join('|').contains("/XML")
          && !win.calls.join('|').contains("schtasks"), "Windows backend only uses native helper without Task Scheduler or XML");
    Scheduler unsupported(dir.filePath("unsupported.json"), {}, Scheduler::Platform::Unsupported);
    check(!unsupported.query().error.isEmpty(), "unsupported platform reports an explicit error");
    qInfo() << "scheduler failures:" << failures;
    return failures ? 1 : 0;
}
