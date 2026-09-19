#include "core/HistoryScheduler.h"

#include "core/ApiErrors.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTime>
#include <QXmlStreamWriter>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
QString describe(const HistoryScheduler::CommandResult &r) {
    if (r.exitCode == 1223) return Loc::get("已取消 UAC 授权，服务与配置未修改");
    if (r.exitCode == 0x20000001) return Loc::get("回滚失败，请刷新并检查实际服务状态");
    if (r.timedOut) return Loc::get("系统调度命令超时");
    if (!r.started) return Loc::get("无法启动系统调度命令") + ": " + r.error.left(4096);
    return Loc::get("系统调度命令失败（退出码 %1）").arg(r.exitCode) + ": "
        + (r.error.isEmpty() ? r.output : r.error).trimmed().left(4096);
}
bool ok(const HistoryScheduler::CommandResult &r) {
    return r.started && !r.timedOut && r.exitCode == 0;
}
bool macMissing(const HistoryScheduler::CommandResult &r) {
    // launchctl uses ESRCH / ENOENT or its bootstrap "service not found" status.
    // EPERM / EACCES must never be misreported as an absent service.
    return r.started && !r.timedOut && (r.exitCode == 3 || r.exitCode == 113);
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(Loc::get("无法读取服务配置") + ": " + file.errorString());
    return file.readAll();
}
void writeFile(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) fail(Loc::get("无法创建服务配置目录"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) fail(Loc::get("无法保存服务配置") + ": " + file.errorString());
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(Loc::get("无法保存服务配置") + ": " + file.errorString());
}
void restoreFile(const QString &path, bool existed, const QByteArray &bytes) {
    if (existed) writeFile(path, bytes);
    else if (QFile::exists(path) && !QFile::remove(path)) fail(Loc::get("无法清理服务配置文件"));
}
QString quoteWindowsArgument(const QString &value) {
    QString result = "\"";
    int slashes = 0;
    for (const QChar c : value) {
        if (c == '\\') { ++slashes; continue; }
        if (c == '"') result += QString(slashes * 2 + 1, '\\') + c;
        else result += QString(slashes, '\\') + c;
        slashes = 0;
    }
    return result + QString(slashes * 2, '\\') + '"';
}
QStringList windowsRuntimeDirectories() {
    QStringList directories{QCoreApplication::applicationDirPath(),
                            QLibraryInfo::path(QLibraryInfo::BinariesPath),
                            QLibraryInfo::path(QLibraryInfo::PluginsPath)};
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                    GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module)) {
            do {
                const QString moduleName = QString::fromWCharArray(module.szModule).toLower();
                if (moduleName.startsWith("qt6") || moduleName.startsWith("libgcc")
                    || moduleName.startsWith("libstdc++") || moduleName.startsWith("libwinpthread")
                    || moduleName.startsWith("vcruntime") || moduleName.startsWith("msvcp"))
                    directories.append(QFileInfo(QString::fromWCharArray(module.szExePath)).absolutePath());
            } while (Module32NextW(snapshot, &module));
        }
        CloseHandle(snapshot);
    }
#endif
    directories.removeDuplicates();
#ifdef Q_OS_WIN
    wchar_t windowsDirectory[MAX_PATH + 1]{};
    const UINT count = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (!count || count >= MAX_PATH) fail(Loc::get("无法定位 Windows 系统目录"));
    const QString windowsRoot = QDir::fromNativeSeparators(QString::fromWCharArray(windowsDirectory));
    // System runtime locations already belong to SCM's system PATH. Do not pass
    // Windows/System32 to the file-permission transaction.
    for (qsizetype i = directories.size(); i-- > 0;) {
        const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(directories[i]));
        if (dir.compare(windowsRoot, Qt::CaseInsensitive) == 0
            || dir.startsWith(windowsRoot + '/', Qt::CaseInsensitive)) directories.removeAt(i);
    }
#endif
    return directories;
}
#ifdef Q_OS_WIN
QString windowsError(DWORD code) {
    wchar_t *buffer = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
            reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString text = size ? QString::fromWCharArray(buffer, int(size)).trimmed() : QString::number(code);
    if (buffer) LocalFree(buffer);
    return text;
}
HistoryScheduler::CommandResult runElevated(const QString &program, const QStringList &arguments) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    struct ComScope { bool initialized; ~ComScope() { if (initialized) CoUninitialize(); } } scope{SUCCEEDED(com)};
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        return {int(com), {}, windowsError(DWORD(com)), false, false};
    QStringList escaped;
    for (const QString &argument : arguments) escaped.append(quoteWindowsArgument(argument));
    const std::wstring executable = QDir::toNativeSeparators(program).toStdWString();
    const std::wstring parameters = escaped.join(' ').toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        return {int(error), {}, windowsError(error), false, false};
    }
    // The native helper bounds SCM waits. Never destroy its staged configuration
    // while an accepted operation is still running, nor kill it mid-rollback.
    const DWORD wait = WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = ERROR_GEN_FAILURE;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &code);
    else code = GetLastError();
    CloseHandle(info.hProcess);
    return {int(code), {}, code ? windowsError(code) : QString()};
}
#endif
HistoryScheduler::CommandResult runProcess(const QString &program, const QStringList &args) {
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();
    HistoryScheduler::CommandResult r;
    if (!process.waitForStarted(5000)) {
        r.started = false;
        r.error = process.errorString();
        return r;
    }
    if (!process.waitForFinished(15000)) {
        r.timedOut = true;
        process.kill();
        process.waitForFinished(3000);
    }
    r.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    r.output = QString::fromUtf8(process.readAllStandardOutput());
    r.error = QString::fromLocal8Bit(process.readAllStandardError());
    return r;
}
}

HistoryScheduler::HistoryScheduler(QString configPath, CommandExecutor executor, Platform platform,
                                   QString launchAgentsDirectory, QString userIdentity)
    : configPath_(std::move(configPath)), launchAgentsDirectory_(std::move(launchAgentsDirectory)),
      userIdentity_(std::move(userIdentity)), executor_(std::move(executor)), platform_(platform) {
    if (!QDir::isAbsolutePath(configPath_)) fail(Loc::get("服务配置必须使用绝对路径"));
    configPath_ = QDir::cleanPath(configPath_);
    if (platform_ == Platform::Native) {
#ifdef Q_OS_MACOS
        platform_ = Platform::MacOS;
#elif defined(Q_OS_WIN)
        platform_ = Platform::Windows;
#else
        platform_ = Platform::Unsupported;
#endif
    }
    if (!executor_) {
        executor_ = [](const QString &program, const QStringList &args) {
#ifdef Q_OS_WIN
            if (program.endsWith("bbhouse-history-service.exe", Qt::CaseInsensitive)
                    && args.value(0) != "--query") return runElevated(program, args);
#endif
            return runProcess(program, args);
        };
    }
    if (launchAgentsDirectory_.isEmpty())
        launchAgentsDirectory_ = QDir::home().filePath("Library/LaunchAgents");
    if (!QDir::isAbsolutePath(launchAgentsDirectory_)) fail(Loc::get("LaunchAgents 目录必须使用绝对路径"));
    if (platform_ == Platform::MacOS && userIdentity_.isEmpty()) {
#ifndef Q_OS_WIN
        userIdentity_ = QString::number(getuid());
#endif
    }
    const QByteArray hash = QCryptographicHash::hash(
        (QDir::homePath() + '\n' + configPath_).toUtf8(), QCryptographicHash::Sha256).toHex().left(20);
    taskName_ = QStringLiteral("com.bbhouse.history.") + QString::fromLatin1(hash);
}

void HistoryScheduler::validateConfig(const HistoryServiceConfig &c) {
    for (const QString &path : {c.executablePath, c.cookiePath, c.databasePath, c.exportPath}) {
        if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null) || path.contains('\n') || path.contains('\r'))
            fail(Loc::get("服务程序、Cookie、数据库和导出文件必须使用有效绝对路径"));
    }
    if (!QRegularExpression(QStringLiteral("^(?:[01][0-9]|2[0-3]):[0-5][0-9]$")).match(c.time).hasMatch())
        fail(Loc::get("请输入有效的运行时间（HH:mm）"));
    if (c.cycle != "daily" && c.cycle != "weekly") fail(Loc::get("请选择每天或每周周期"));
    if (c.weekday < 1 || c.weekday > 7) fail(Loc::get("请选择有效的星期"));
}
HistoryServiceConfig HistoryScheduler::loadConfig(const QString &path) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(readFile(path), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) fail(Loc::get("服务配置格式无效"));
    const QJsonObject object = document.object();
    HistoryServiceConfig c;
    c.executablePath = object.value("executablePath").toString();
    c.cookiePath = object.value("cookiePath").toString();
    c.databasePath = object.value("databasePath").toString();
    c.exportPath = object.value("exportPath").toString();
    c.time = object.value("time").toString("01:00");
    c.cycle = object.value("cycle").toString("daily");
    c.weekday = object.value("weekday").toInt(1);
    c.enabled = object.value("enabled").toBool(true);
    if ((object.contains("enabled") && !object.value("enabled").isBool()) ||
        (object.contains("weekday") && (!object.value("weekday").isDouble() || object.value("weekday").toDouble() != c.weekday)))
        fail(Loc::get("服务配置格式无效"));
    validateConfig(c);
    return c;
}
void HistoryScheduler::saveConfig(const QString &path, const HistoryServiceConfig &c) {
    validateConfig(c);
    if (!QDir::isAbsolutePath(path)) fail(Loc::get("服务配置必须使用绝对路径"));
    writeFile(path, QJsonDocument(QJsonObject{{"version", 1}, {"executablePath", c.executablePath},
        {"cookiePath", c.cookiePath}, {"databasePath", c.databasePath}, {"exportPath", c.exportPath},
        {"time", c.time}, {"cycle", c.cycle}, {"weekday", c.weekday}, {"enabled", c.enabled}}).toJson());
}

QByteArray HistoryScheduler::launchAgentPlist(const HistoryServiceConfig &c, const QString &path, const QString &label) {
    validateConfig(c);
    QByteArray bytes;
    QXmlStreamWriter w(&bytes);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeDTD("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
    w.writeStartElement("plist"); w.writeAttribute("version", "1.0"); w.writeStartElement("dict");
    const auto key = [&](const QString &k) { w.writeTextElement("key", k); };
    key("Label"); w.writeTextElement("string", label);
    key("ProgramArguments"); w.writeStartElement("array");
    for (const QString &arg : {c.executablePath, QStringLiteral("--history-sync-once"), QStringLiteral("--config"), path})
        w.writeTextElement("string", arg);
    w.writeEndElement();
    key("RunAtLoad"); w.writeEmptyElement("false");
    key("KeepAlive"); w.writeEmptyElement("false");
    key("Disabled"); w.writeEmptyElement(c.enabled ? "false" : "true");
    key("StartCalendarInterval"); w.writeStartElement("dict");
    const QTime time = QTime::fromString(c.time, "HH:mm");
    key("Hour"); w.writeTextElement("integer", QString::number(time.hour()));
    key("Minute"); w.writeTextElement("integer", QString::number(time.minute()));
    if (c.cycle == "weekly") { key("Weekday"); w.writeTextElement("integer", QString::number(c.weekday % 7)); }
    w.writeEndElement(); w.writeEndElement(); w.writeEndElement(); w.writeEndDocument();
    return bytes;
}
QString HistoryScheduler::launchAgentPath() const { return QDir(launchAgentsDirectory_).filePath(taskName_ + ".plist"); }
QString HistoryScheduler::domain() const { return "gui/" + userIdentity_; }
HistoryScheduler::CommandResult HistoryScheduler::command(const QStringList &args) {
    if (platform_ == Platform::MacOS) return executor_("/bin/launchctl", args);
    if (platform_ == Platform::Windows)
        return executor_(QDir(QCoreApplication::applicationDirPath()).filePath("bbhouse-history-service.exe"), args);
    fail(Loc::get("当前系统不支持定时历史服务"));
}
void HistoryScheduler::checked(const QStringList &args) {
    const CommandResult r = command(args);
    if (!ok(r)) fail(describe(r));
}
HistoryServiceStatus HistoryScheduler::query() {
    HistoryServiceStatus status;
    try {
        if (platform_ == Platform::MacOS) {
            const auto result = command({"print", domain() + "/" + taskName_});
            const bool loaded = ok(result);
            if (!loaded && !macMissing(result)) fail(describe(result));
            const auto disabled = command({"print-disabled", domain()});
            if (!ok(disabled)) fail(describe(disabled));
            const auto match = QRegularExpression("\"" + QRegularExpression::escape(taskName_) + "\"\\s*=>\\s*(true|false)").match(disabled.output);
            status.registered = loaded || QFileInfo::exists(launchAgentPath());
            status.enabled = loaded && (!match.hasMatch() || match.captured(1) == "false");
            const auto lastExit = QRegularExpression("last exit code = (-?[0-9]+)").match(result.output);
            if (lastExit.hasMatch() && lastExit.captured(1) != "0")
                status.diagnostic = Loc::get("最近系统执行结果：%1；若没有同步日志，请检查程序和数据目录的访问权限").arg(lastExit.captured(1));
        } else if (platform_ == Platform::Windows) {
            const auto result = command({"--query", "--name", taskName_});
            if (!ok(result)) fail(describe(result));
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(result.output.toUtf8(), &error);
            const auto object = document.object();
            if (error.error != QJsonParseError::NoError || !document.isObject()
                    || !object.value("registered").isBool() || !object.value("enabled").isBool())
                fail(Loc::get("系统服务状态格式无效"));
            status.registered = object.value("registered").toBool();
            status.enabled = object.value("enabled").toBool();
            const int code = object.value("exitCode").toInt();
            if (code != 0)
                status.diagnostic = Loc::get("最近系统执行结果：%1；若没有同步日志，请检查程序和数据目录的访问权限").arg(code);
            else if (status.registered && status.enabled && !object.value("running").toBool())
                status.diagnostic = Loc::get("Windows 服务已启用但尚未运行，请保存计划重试并检查运行库与文件权限");
        } else fail(Loc::get("当前系统不支持定时历史服务"));
    } catch (const std::exception &e) { status.error = QString::fromUtf8(e.what()); }
    return status;
}

void HistoryScheduler::applyMac(const HistoryServiceConfig &c) {
    const auto loaded = command({"print", domain() + "/" + taskName_});
    if (ok(loaded)) checked({"bootout", domain() + "/" + taskName_});
    else if (!macMissing(loaded)) fail(describe(loaded));
    writeFile(launchAgentPath(), launchAgentPlist(c, configPath_, taskName_));
    checked({c.enabled ? "enable" : "disable", domain() + "/" + taskName_});
    if (c.enabled) checked({"bootstrap", domain(), launchAgentPath()});
}
void HistoryScheduler::mutateWindows(const HistoryServiceConfig &c, bool requireRegistered) {
    validateConfig(c);
    if (!QFileInfo(c.executablePath).isFile())
        fail(Loc::get("服务程序不存在或不可执行，请重新选择应用位置"));
    const auto before = query();
    if (!before.error.isEmpty()) fail(before.error);
    if (requireRegistered && !before.registered) fail(Loc::get("请先注册定时历史服务"));
    if (!QDir().mkpath(QFileInfo(configPath_).absolutePath())) fail(Loc::get("无法创建服务配置目录"));
    QTemporaryDir request(QFileInfo(configPath_).absolutePath() + "/history-service-request-XXXXXX");
    if (!request.isValid()) fail(Loc::get("无法创建系统任务临时文件"));
    const QString pending = request.filePath("config.json");
    saveConfig(pending, c);
    // Only the elevated helper publishes this file, after UAC approval. It owns
    // the SCM/config/ACL transaction, including rollback; never re-prompt for rollback.
    checked({before.registered ? "--update" : "--install", "--name", taskName_,
             "--config", configPath_, "--pending-config", pending, "--worker", c.executablePath,
             "--runtime-path", windowsRuntimeDirectories().join(';'), "--cookie", c.cookiePath,
             "--database", c.databasePath, "--export", c.exportPath,
             "--enabled", c.enabled ? "1" : "0"});
    const auto after = query();
    if (!after.error.isEmpty()) fail(after.error);
    if (!after.registered || after.enabled != c.enabled)
        fail(Loc::get("系统任务状态与保存的配置不一致"));
}
void HistoryScheduler::apply(const HistoryServiceConfig &c) {
    saveConfig(configPath_, c);
    if (platform_ == Platform::MacOS) applyMac(c);
    else fail(Loc::get("当前系统不支持定时历史服务"));
}
void HistoryScheduler::removeTask() {
    if (platform_ == Platform::MacOS) {
        const auto loaded = command({"print", domain() + "/" + taskName_});
        if (ok(loaded)) checked({"bootout", domain() + "/" + taskName_});
        else if (!macMissing(loaded)) fail(describe(loaded));
        checked({"enable", domain() + "/" + taskName_});
        if (QFile::exists(launchAgentPath()) && !QFile::remove(launchAgentPath()))
            fail(Loc::get("无法移除 LaunchAgent 注册文件，请检查目录权限"));
    } else fail(Loc::get("当前系统不支持定时历史服务"));
}
void HistoryScheduler::mutate(const HistoryServiceConfig &c, bool requireRegistered) {
    if (platform_ == Platform::Windows) { mutateWindows(c, requireRegistered); return; }
    validateConfig(c);
    const QFileInfo executable(c.executablePath);
    if (!executable.isFile() || (platform_ != Platform::Windows && !executable.isExecutable()))
        fail(Loc::get("服务程序不存在或不可执行，请重新选择应用位置"));
    const auto before = query();
    if (!before.error.isEmpty()) fail(before.error);
    if (requireRegistered && !before.registered) fail(Loc::get("请先注册定时历史服务"));
    const bool existed = QFile::exists(configPath_);
    const QByteArray previous = existed ? readFile(configPath_) : QByteArray();
    const bool plistExisted = platform_ == Platform::MacOS && QFile::exists(launchAgentPath());
    const QByteArray previousPlist = plistExisted ? readFile(launchAgentPath()) : QByteArray();
    HistoryServiceConfig old;
    if (before.registered) {
        if (!existed) fail(Loc::get("系统任务已注册但本地配置缺失，请先注销后重新注册"));
        old = loadConfig(configPath_);
        old.enabled = before.enabled;
    }
    try {
        apply(c);
        const auto after = query();
        if (!after.error.isEmpty()) fail(after.error);
        if (!after.registered || after.enabled != c.enabled) fail(Loc::get("系统任务状态与保存的配置不一致"));
    } catch (const std::exception &e) {
        QString message = QString::fromUtf8(e.what());
        try {
            if (before.registered) apply(old);
            else removeTask();
            restoreFile(configPath_, existed, previous);
            if (platform_ == Platform::MacOS) restoreFile(launchAgentPath(), plistExisted, previousPlist);
            const auto restored = query();
            if (!restored.error.isEmpty() || restored.registered != before.registered || restored.enabled != before.enabled)
                fail(restored.error.isEmpty() ? Loc::get("回滚后的系统任务状态不一致") : restored.error);
        } catch (const std::exception &rollback) {
            message += "\n" + Loc::get("回滚失败，请刷新并检查实际服务状态") + ": " + QString::fromUtf8(rollback.what());
        }
        fail(message);
    }
}
void HistoryScheduler::install(const HistoryServiceConfig &c) { mutate(c, false); }
void HistoryScheduler::update(const HistoryServiceConfig &c) { mutate(c, true); }
void HistoryScheduler::setEnabled(bool enabled) {
    HistoryServiceConfig c = loadConfig(configPath_); c.enabled = enabled; update(c);
}
void HistoryScheduler::uninstall() {
    if (platform_ == Platform::Windows) {
        const auto before = query();
        if (!before.error.isEmpty()) fail(before.error);
        if (!before.registered) return;
        checked({"--uninstall", "--name", taskName_});
        const auto after = query();
        if (!after.error.isEmpty()) fail(after.error);
        if (after.registered) fail(Loc::get("服务注销后仍存在，请检查系统任务状态"));
        return;
    }
    const auto before = query();
    if (!before.error.isEmpty()) fail(before.error);
    if (!before.registered) return;
    // Capture the native definition, rather than relying on a possibly deleted or
    // externally edited JSON configuration. Uninstall never modifies user data/config.
    const bool plistExisted = platform_ == Platform::MacOS && QFile::exists(launchAgentPath());
    QByteArray nativeDefinition;
    if (platform_ == Platform::MacOS) {
        if (plistExisted) nativeDefinition = readFile(launchAgentPath());
        else if (QFile::exists(configPath_)) {
            auto c = loadConfig(configPath_); c.enabled = before.enabled;
            nativeDefinition = launchAgentPlist(c, configPath_, taskName_);
        } else fail(Loc::get("系统任务定义与配置均缺失，无法安全注销，请检查系统任务"));
    }
    try {
        removeTask();
        const auto after = query();
        if (!after.error.isEmpty()) fail(after.error);
        if (after.registered) fail(Loc::get("服务注销后仍存在，请检查系统任务状态"));
    } catch (const std::exception &e) {
        QString message = QString::fromUtf8(e.what());
        try {
            if (platform_ == Platform::MacOS) {
                const auto loaded = command({"print", domain() + "/" + taskName_});
                if (ok(loaded)) checked({"bootout", domain() + "/" + taskName_});
                else if (!macMissing(loaded)) fail(describe(loaded));
                writeFile(launchAgentPath(), nativeDefinition);
                checked({before.enabled ? "enable" : "disable", domain() + "/" + taskName_});
                if (before.enabled) checked({"bootstrap", domain(), launchAgentPath()});
                if (!plistExisted && !QFile::remove(launchAgentPath())) fail(Loc::get("无法清理服务配置文件"));
            }
            const auto restored = query();
            if (!restored.error.isEmpty() || restored.registered != before.registered || restored.enabled != before.enabled)
                fail(restored.error.isEmpty() ? Loc::get("回滚后的系统任务状态不一致") : restored.error);
        } catch (const std::exception &rollback) {
            message += "\n" + Loc::get("回滚失败，请刷新并检查实际服务状态") + ": " + QString::fromUtf8(rollback.what());
        }
        fail(message);
    }
}
