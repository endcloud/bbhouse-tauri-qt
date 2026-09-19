// This executable deliberately has no Qt dependency: SCM and UAC must also work
// when the GUI was launched with Qt Creator's private Debug DLL environment.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <shellapi.h>
#include <sddl.h>
#include <taskschd.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr DWORD RollbackFailed = 0x20000001;

DWORD rollbackResult(DWORD operationError, DWORD rollbackError)
{
    if (!rollbackError) return operationError;
    std::fprintf(stderr, "Windows service rollback failed: operation=%lu rollback=%lu\n",
        static_cast<unsigned long>(operationError), static_cast<unsigned long>(rollbackError));
    return RollbackFailed;
}

class Handle {
public:
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const { return value; }
private:
    HANDLE value;
};

class ServiceHandle {
public:
    explicit ServiceHandle(SC_HANDLE handle = nullptr) : value(handle) {}
    ~ServiceHandle() { if (value) CloseServiceHandle(value); }
    ServiceHandle(const ServiceHandle &) = delete;
    ServiceHandle &operator=(const ServiceHandle &) = delete;
    SC_HANDLE get() const { return value; }
private:
    SC_HANDLE value;
};

struct Options {
    std::wstring action, name, config, pendingConfig, worker, runtimePath, cookie, database, exportPath;
    std::wstring enabled = L"1";
};

std::wstring quote(const std::wstring &value)
{
    // CommandLineToArgvW / MSVCRT escaping, including trailing backslashes.
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}

bool absolutePath(const std::wstring &path)
{
    // Local absolute paths only: a LocalService service cannot use the GUI's
    // mapped/network credentials. Reject device namespaces and alternate streams.
    if (path.size() < 3 || !iswalpha(path[0]) || path[1] != L':'
        || (path[2] != L'\\' && path[2] != L'/')) return false;
    if (path.find(L':', 2) != std::wstring::npos
        || path.find_first_of(L"\r\n\"*") != std::wstring::npos) return false;
    size_t begin = 3;
    while (begin <= path.size()) {
        size_t end = path.find_first_of(L"\\/", begin);
        if (end == std::wstring::npos) end = path.size();
        const auto part = path.substr(begin, end - begin);
        if (part == L"." || part == L"..") return false;
        begin = end + 1;
    }
    return true;
}

std::wstring parentPath(const std::wstring &path)
{
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    return path.substr(0, separator == 2 ? 3 : separator);
}

std::wstring executablePath()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

bool systemDirectoryTree(const std::wstring &path)
{
    std::vector<wchar_t> buffer(32768);
    const UINT length = GetWindowsDirectoryW(buffer.data(), UINT(buffer.size()));
    if (!length || length >= buffer.size()) return true; // Fail closed for ACL changes.
    std::wstring directory(buffer.data(), length), candidate(path);
    std::replace(directory.begin(), directory.end(), L'/', L'\\');
    std::replace(candidate.begin(), candidate.end(), L'/', L'\\');
    while (directory.size() > 3 && directory.back() == L'\\') directory.pop_back();
    return _wcsicmp(candidate.c_str(), directory.c_str()) == 0
        || (candidate.size() > directory.size() && candidate[directory.size()] == L'\\'
            && _wcsnicmp(candidate.c_str(), directory.c_str(), directory.size()) == 0);
}

std::vector<std::wstring> runtimeDirectories(const std::wstring &path)
{
    std::vector<std::wstring> result;
    size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find(L';', begin);
        auto directory = path.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (!directory.empty()) result.push_back(std::move(directory));
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return result;
}

DWORD parseOptions(Options &options)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return GetLastError();
    std::map<std::wstring, std::wstring *> fields{
        {L"--name", &options.name}, {L"--config", &options.config}, {L"--pending-config", &options.pendingConfig},
        {L"--worker", &options.worker}, {L"--runtime-path", &options.runtimePath},
        {L"--cookie", &options.cookie}, {L"--database", &options.database},
        {L"--export", &options.exportPath}, {L"--enabled", &options.enabled}};
    const std::vector<std::wstring> actions{
        L"--query", L"--install", L"--update", L"--enable", L"--disable", L"--uninstall", L"--run"};
    std::vector<std::wstring> used;
    DWORD error = ERROR_SUCCESS;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (std::find(actions.begin(), actions.end(), argument) != actions.end()) {
            if (!options.action.empty()) { error = ERROR_INVALID_PARAMETER; break; }
            options.action = argument;
        } else {
            const auto found = fields.find(argument);
            if (found == fields.end() || ++i >= argc
                || std::find(used.begin(), used.end(), argument) != used.end()) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            *found->second = argv[i];
            used.push_back(argument);
        }
    }
    LocalFree(argv);
    const std::wstring prefix = L"com.bbhouse.history.";
    if (error || options.action.empty() || options.name.size() <= prefix.size()
        || options.name.size() > 128 || options.name.compare(0, prefix.size(), prefix) != 0)
        return ERROR_INVALID_PARAMETER;
    for (const wchar_t ch : options.name.substr(prefix.size()))
        if (!iswxdigit(ch)) return ERROR_INVALID_PARAMETER;
    if (options.action == L"--run" || options.action == L"--install" || options.action == L"--update") {
        if (!absolutePath(options.worker) || !absolutePath(options.config)) return ERROR_BAD_PATHNAME;
        for (const auto &directory : runtimeDirectories(options.runtimePath))
            if (!absolutePath(directory)) return ERROR_BAD_PATHNAME;
    }
    if (options.action == L"--install" || options.action == L"--update") {
        if ((options.enabled != L"0" && options.enabled != L"1")
            || !absolutePath(options.pendingConfig) || !absolutePath(options.cookie) || !absolutePath(options.database)
            || !absolutePath(options.exportPath)) return ERROR_BAD_PATHNAME;
    }
    return ERROR_SUCCESS;
}

DWORD queryStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS &status)
{
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE *>(&status), sizeof(status), &bytes))
        return GetLastError();
    return ERROR_SUCCESS;
}

DWORD queryConfig(SC_HANDLE service, std::vector<BYTE> &buffer)
{
    DWORD size = 0;
    QueryServiceConfigW(service, nullptr, 0, &size);
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) return error;
    buffer.resize(size);
    if (!QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data()),
                            size, &size)) return GetLastError();
    return ERROR_SUCCESS;
}

DWORD waitForState(SC_HANDLE service, DWORD target, DWORD timeout = 30000)
{
    const ULONGLONG deadline = GetTickCount64() + timeout;
    do {
        SERVICE_STATUS_PROCESS status{};
        const DWORD error = queryStatus(service, status);
        if (error) return error;
        if (status.dwCurrentState == target) return ERROR_SUCCESS;
        if (target == SERVICE_RUNNING && status.dwCurrentState == SERVICE_STOPPED) {
            if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR)
                return status.dwServiceSpecificExitCode ? status.dwServiceSpecificExitCode : ERROR_PROCESS_ABORTED;
            return status.dwWin32ExitCode ? status.dwWin32ExitCode : ERROR_PROCESS_ABORTED;
        }
        Sleep(100);
    } while (GetTickCount64() < deadline);
    return ERROR_SERVICE_REQUEST_TIMEOUT;
}

DWORD stopService(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status{};
    DWORD error = queryStatus(service, status);
    if (error || status.dwCurrentState == SERVICE_STOPPED) return error;
    if (status.dwCurrentState == SERVICE_START_PENDING) {
        error = waitForState(service, SERVICE_RUNNING);
        if (error) {
            if (queryStatus(service, status) == ERROR_SUCCESS && status.dwCurrentState == SERVICE_STOPPED)
                return ERROR_SUCCESS;
            return error;
        }
    }
    if (status.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS result{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &result)) {
            error = GetLastError();
            if (error == ERROR_SERVICE_NOT_ACTIVE) return ERROR_SUCCESS;
            return error;
        }
    }
    return waitForState(service, SERVICE_STOPPED, 60000);
}

DWORD startService(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS status{};
    DWORD error = queryStatus(service, status);
    if (error) return error;
    if (status.dwCurrentState == SERVICE_RUNNING) return ERROR_SUCCESS;
    if (status.dwCurrentState == SERVICE_STOP_PENDING) {
        error = waitForState(service, SERVICE_STOPPED, 60000);
        if (error) return error;
    }
    if (status.dwCurrentState != SERVICE_START_PENDING && !StartServiceW(service, 0, nullptr)) {
        error = GetLastError();
        if (error != ERROR_SERVICE_ALREADY_RUNNING) return error;
    }
    return waitForState(service, SERVICE_RUNNING);
}

DWORD query(const Options &options)
{
    // The default SCM service DACL grants local authenticated users these read
    // rights. Request neither READ_CONTROL nor any administrative mutation right,
    // so the ordinary GUI process can query a service created by elevated UAC.
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager.get()) return GetLastError();
    ServiceHandle service(OpenServiceW(manager.get(), options.name.c_str(), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS));
    if (!service.get()) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST) return error;
        std::puts("{\"registered\":false,\"enabled\":false,\"running\":false,\"exitCode\":0}");
        return ERROR_SUCCESS;
    }
    std::vector<BYTE> buffer;
    DWORD error = queryConfig(service.get(), buffer);
    if (error) return error;
    SERVICE_STATUS_PROCESS status{};
    error = queryStatus(service.get(), status);
    if (error) return error;
    const auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    const DWORD exitCode = status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR
        ? status.dwServiceSpecificExitCode : status.dwWin32ExitCode;
    std::printf("{\"registered\":true,\"enabled\":%s,\"running\":%s,\"exitCode\":%lu}\n",
                config->dwStartType != SERVICE_DISABLED ? "true" : "false",
                status.dwCurrentState == SERVICE_RUNNING ? "true" : "false",
                static_cast<unsigned long>(exitCode));
    return ERROR_SUCCESS;
}

class AclTransaction {
public:
    DWORD rollback()
    {
        DWORD firstError = ERROR_SUCCESS;
        if (!committed) {
            for (auto item = originals.rbegin(); item != originals.rend(); ++item) {
                PACL acl = nullptr;
                BOOL present = FALSE, defaulted = FALSE;
                if (GetSecurityDescriptorDacl(item->descriptor, &present, &acl, &defaulted)) {
                    SECURITY_DESCRIPTOR_CONTROL control{};
                    DWORD revision = 0;
                    if (!GetSecurityDescriptorControl(item->descriptor, &control, &revision)) {
                        if (!firstError) firstError = GetLastError();
                        continue;
                    }
                    const DWORD error = SetNamedSecurityInfoW(const_cast<wchar_t *>(item->path.c_str()), SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED)
                            ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                        nullptr, nullptr, present ? acl : nullptr, nullptr);
                    if (error && !firstError) firstError = error;
                } else if (!firstError) {
                    firstError = GetLastError();
                }
            }
        }
        committed = true;
        return firstError;
    }

    ~AclTransaction()
    {
        rollback();
        for (const auto &item : originals) LocalFree(item.descriptor);
    }

    DWORD grant(const std::wstring &path, PSID sid, DWORD rights, bool directory, bool readOnlyCookie = false)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
        if (directory != bool(attributes & FILE_ATTRIBUTE_DIRECTORY)) return ERROR_DIRECTORY;
        // Do not grant through junctions/symlinks supplied to an elevated helper.
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_REPARSE_TAG_INVALID;
        PACL oldAcl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        DWORD error = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                           nullptr, nullptr, &oldAcl, nullptr, &descriptor);
        if (error) return error;
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = rights;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = directory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
        std::vector<BYTE> cookieAcl;
        if (readOnlyCookie && oldAcl) {
            // A config/database parent may also contain Cookie.txt. Preserve
            // every other trustee, but prevent its inherited Modify ACE from
            // overriding the service SID's explicitly read-only Cookie grant.
            cookieAcl.resize(oldAcl->AclSize);
            PACL restricted = reinterpret_cast<PACL>(cookieAcl.data());
            if (!InitializeAcl(restricted, DWORD(cookieAcl.size()), ACL_REVISION)) {
                error = GetLastError();
            } else {
                for (DWORD index = 0; index < oldAcl->AceCount && !error; ++index) {
                    void *rawAce = nullptr;
                    if (!GetAce(oldAcl, index, &rawAce)) { error = GetLastError(); break; }
                    auto *header = static_cast<ACE_HEADER *>(rawAce);
                    bool serviceAce = false;
                    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE || header->AceType == ACCESS_DENIED_ACE_TYPE) {
                        const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(rawAce);
                        serviceAce = EqualSid(const_cast<DWORD *>(&ace->SidStart), sid);
                    }
                    if (serviceAce) continue;
                    // Keeping existing access while making the DACL protected
                    // requires converting its inherited ACEs into explicit ACEs.
                    std::vector<BYTE> copy(header->AceSize);
                    std::copy_n(static_cast<BYTE *>(rawAce), header->AceSize, copy.data());
                    reinterpret_cast<ACE_HEADER *>(copy.data())->AceFlags &= ~INHERITED_ACE;
                    if (!AddAce(restricted, ACL_REVISION, MAXDWORD, copy.data(), DWORD(copy.size())))
                        error = GetLastError();
                }
            }
            if (error) { LocalFree(descriptor); return error; }
            oldAcl = restricted;
        }
        PACL newAcl = nullptr;
        error = SetEntriesInAclW(1, &access, oldAcl, &newAcl);
        if (!error) {
            // Retain the snapshot even if SetNamedSecurityInfo reports a partial
            // inheritance failure, so rollback still attempts the original ACL.
            originals.push_back({path, descriptor});
            descriptor = nullptr;
            error = SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | (readOnlyCookie ? PROTECTED_DACL_SECURITY_INFORMATION : 0),
                nullptr, nullptr, newAcl, nullptr);
        }
        if (newAcl) LocalFree(newAcl);
        if (error) { LocalFree(descriptor); return error; }
        return ERROR_SUCCESS;
    }

    void commit() { committed = true; }
private:
    struct Original { std::wstring path; PSECURITY_DESCRIPTOR descriptor; };
    std::vector<Original> originals;
    bool committed = false;
};

class ConfigTransaction {
public:
    ~ConfigTransaction() { rollback(); if (originalSecurity) LocalFree(originalSecurity); }
    DWORD replace(const std::wstring &target, const std::wstring &pending)
    {
        path = target;
        std::vector<BYTE> next;
        DWORD error = read(pending, next);
        if (error) return error;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        exists = attributes != INVALID_FILE_ATTRIBUTES;
        if (!exists && GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
        if (exists) {
            error = read(path, previous);
            if (error) return error;
            error = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                nullptr, nullptr, nullptr, nullptr, &originalSecurity);
            if (error) return error;
        }
        active = true;
        return write(path, next);
    }
    void commit() { active = false; }
    DWORD rollback()
    {
        if (!active) return ERROR_SUCCESS;
        DWORD error = ERROR_SUCCESS;
        if (exists) {
            error = write(path, previous);
            // ReplaceFile's rare partial failures can leave the target absent;
            // re-creating it must restore the original ACL, not just its bytes.
            if (!error && originalSecurity) {
                PACL acl = nullptr;
                BOOL present = FALSE, defaulted = FALSE;
                SECURITY_DESCRIPTOR_CONTROL control{};
                DWORD revision = 0;
                if (!GetSecurityDescriptorDacl(originalSecurity, &present, &acl, &defaulted)
                    || !GetSecurityDescriptorControl(originalSecurity, &control, &revision)) {
                    error = GetLastError();
                } else {
                    error = SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED)
                            ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                        nullptr, nullptr, present ? acl : nullptr, nullptr);
                }
            }
        }
        else if (!DeleteFileW(path.c_str())) {
            error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
        }
        active = false;
        return error;
    }
private:
    static DWORD read(const std::wstring &path, std::vector<BYTE> &bytes)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return ERROR_BAD_PATHNAME;
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) return GetLastError();
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size)) return GetLastError();
        if (size.QuadPart < 2 || size.QuadPart > 1024 * 1024) return ERROR_INVALID_DATA;
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD count = 0;
        if (!ReadFile(file.get(), bytes.data(), DWORD(bytes.size()), &count, nullptr)) return GetLastError();
        return count == bytes.size() ? ERROR_SUCCESS : ERROR_READ_FAULT;
    }
    static DWORD write(const std::wstring &path, const std::vector<BYTE> &bytes)
    {
        // Write and flush a unique same-directory file, then publish atomically.
        // ReplaceFile preserves the existing target's DACL. Its normal ACL merge
        // errors remain fatal instead of silently broadening permissions.
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return ERROR_BAD_PATHNAME;
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
            return GetLastError();
        GUID identifier{};
        const HRESULT result = CoCreateGuid(&identifier);
        if (FAILED(result)) return static_cast<DWORD>(result);
        wchar_t suffix[40]{};
        if (!StringFromGUID2(identifier, suffix, 40)) return ERROR_INVALID_DATA;
        struct TemporaryFile {
            std::wstring path;
            ~TemporaryFile() { if (!path.empty()) DeleteFileW(path.c_str()); }
        } temporary{path + L"." + suffix + L".tmp"};
        {
            Handle file(CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.get() == INVALID_HANDLE_VALUE) {
                // Never remove a pre-existing file if a GUID collision occurs.
                const DWORD error = GetLastError();
                temporary.path.clear();
                return error;
            }
            DWORD count = 0;
            if (!WriteFile(file.get(), bytes.data(), DWORD(bytes.size()), &count, nullptr)) return GetLastError();
            if (count != bytes.size()) return ERROR_WRITE_FAULT;
            if (!FlushFileBuffers(file.get())) return GetLastError();
        }
        const BOOL published = attributes != INVALID_FILE_ATTRIBUTES
            ? ReplaceFileW(path.c_str(), temporary.path.c_str(), nullptr, 0, nullptr, nullptr)
            : MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!published) return GetLastError();
        temporary.path.clear();
        return ERROR_SUCCESS;
    }
    std::wstring path;
    std::vector<BYTE> previous;
    PSECURITY_DESCRIPTOR originalSecurity = nullptr;
    bool exists = false, active = false;
};

DWORD grantServicePaths(const Options &options, AclTransaction &transaction)
{
    const std::wstring account = L"NT SERVICE\\" + options.name;
    DWORD sidSize = 0, domainSize = 0;
    SID_NAME_USE use{};
    LookupAccountNameW(nullptr, account.c_str(), nullptr, &sidSize, nullptr, &domainSize, &use);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return GetLastError();
    std::vector<BYTE> sid(sidSize);
    std::vector<wchar_t> domain(domainSize);
    if (!LookupAccountNameW(nullptr, account.c_str(), sid.data(), &sidSize,
                            domain.data(), &domainSize, &use)) return GetLastError();
    const DWORD readExecute = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    const DWORD modify = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
    std::vector<std::pair<std::wstring, DWORD>> directories{
        {parentPath(executablePath()), readExecute}, {parentPath(options.worker), readExecute},
        {parentPath(options.config), modify}, {parentPath(options.database), modify},
        {parentPath(options.exportPath), modify}};
    for (const auto &directory : runtimeDirectories(options.runtimePath))
        directories.emplace_back(directory, readExecute);
    for (const auto &directory : directories) {
        // LocalService normally has SeChangeNotifyPrivilege, which permits
        // traversing user-profile ancestors; only the actual working/dependency
        // directory needs RX. Never recursively alter a drive or Windows tree.
        if (directory.first.size() <= 3 || systemDirectoryTree(directory.first)) {
            if (directory.second == modify) return ERROR_ACCESS_DENIED;
            continue;
        }
        const DWORD error = transaction.grant(directory.first, sid.data(), directory.second, true);
        if (error) return error;
    }
    // Apply explicitly as well: existing files can have protected, non-inheriting ACLs.
    DWORD error = transaction.grant(options.config, sid.data(), modify, false);
    if (error) return error;
    error = transaction.grant(options.cookie, sid.data(), FILE_GENERIC_READ, false, true);
    if (error) return error;
    for (const auto &path : {options.database, options.exportPath}) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            error = transaction.grant(path, sid.data(), modify, false);
            if (error) return error;
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND) return GetLastError();
    }
    return ERROR_SUCCESS;
}

std::wstring serviceCommand(const Options &options)
{
    return quote(executablePath()) + L" --run --name " + quote(options.name)
        + L" --worker " + quote(options.worker) + L" --config " + quote(options.config)
        + L" --runtime-path " + quote(options.runtimePath);
}

DWORD removeLegacyTask(const std::wstring &name)
{
    // Migration only. New registrations never generate/import task XML or run
    // schtasks. Delete precisely the old per-configuration task at the root.
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result)) return static_cast<DWORD>(result);
    ITaskService *scheduler = nullptr;
    ITaskFolder *folder = nullptr;
    result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITaskService, reinterpret_cast<void **>(&scheduler));
    if (SUCCEEDED(result)) {
        VARIANT empty;
        VariantInit(&empty);
        result = scheduler->Connect(empty, empty, empty, empty);
    }
    if (SUCCEEDED(result)) {
        BSTR root = SysAllocString(L"\\");
        result = root ? scheduler->GetFolder(root, &folder) : E_OUTOFMEMORY;
        SysFreeString(root);
    }
    if (SUCCEEDED(result)) {
        BSTR task = SysAllocString(name.c_str());
        result = task ? folder->DeleteTask(task, 0) : E_OUTOFMEMORY;
        SysFreeString(task);
        if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
            || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) result = S_OK;
    }
    if (folder) folder->Release();
    if (scheduler) scheduler->Release();
    CoUninitialize();
    return SUCCEEDED(result) ? ERROR_SUCCESS
        : HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : static_cast<DWORD>(result);
}

DWORD mutate(const Options &options)
{
    const bool configure = options.action == L"--install" || options.action == L"--update";
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr,
        SC_MANAGER_CONNECT | (configure ? SC_MANAGER_CREATE_SERVICE : 0)));
    if (!manager.get()) return GetLastError();
    constexpr DWORD rights = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START
        | SERVICE_STOP | SERVICE_CHANGE_CONFIG | DELETE;
    SC_HANDLE rawService = OpenServiceW(manager.get(), options.name.c_str(), rights);
    bool created = false;
    if (!rawService) {
        DWORD error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST) return error;
        if (options.action == L"--uninstall") return ERROR_SUCCESS;
        if (!configure) return error;
        const auto command = serviceCommand(options);
        const std::wstring display = L"BBHouse History (" + options.name.substr(20) + L")";
        rawService = CreateServiceW(manager.get(), options.name.c_str(), display.c_str(), rights,
            SERVICE_WIN32_OWN_PROCESS, options.enabled == L"1" ? SERVICE_AUTO_START : SERVICE_DISABLED, SERVICE_ERROR_NORMAL,
            command.c_str(), nullptr, nullptr, nullptr, L"NT AUTHORITY\\LocalService", nullptr);
        if (!rawService) return GetLastError();
        created = true;
    }
    ServiceHandle service(rawService);
    const auto failedBeforeMutation = [&](DWORD error) {
        if (created && !DeleteService(service.get())) return rollbackResult(error, GetLastError());
        return error;
    };
    if (options.action == L"--uninstall") {
        SERVICE_STATUS_PROCESS status{};
        DWORD error = queryStatus(service.get(), status);
        if (error) return error;
        const bool running = status.dwCurrentState == SERVICE_RUNNING || status.dwCurrentState == SERVICE_START_PENDING;
        error = stopService(service.get());
        if (error) {
            return rollbackResult(error, running ? startService(service.get()) : ERROR_SUCCESS);
        }
        if (!DeleteService(service.get())) {
            error = GetLastError();
            return rollbackResult(error, running ? startService(service.get()) : ERROR_SUCCESS);
        }
        return ERROR_SUCCESS;
    }
    std::vector<BYTE> oldConfigBuffer;
    DWORD error = queryConfig(service.get(), oldConfigBuffer);
    if (error) return failedBeforeMutation(error);
    const auto *oldConfig = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(oldConfigBuffer.data());
    // Only services using our unprivileged account may be changed by this helper.
    if (_wcsicmp(oldConfig->lpServiceStartName, L"NT AUTHORITY\\LocalService") != 0) {
        return failedBeforeMutation(ERROR_ACCESS_DENIED);
    }
    SERVICE_STATUS_PROCESS oldStatus{};
    error = queryStatus(service.get(), oldStatus);
    if (error) return failedBeforeMutation(error);
    const bool wasRunning = oldStatus.dwCurrentState == SERVICE_RUNNING
        || oldStatus.dwCurrentState == SERVICE_START_PENDING;
    SERVICE_SID_INFO oldSidInfo{};
    DWORD bytes = 0;
    if (!QueryServiceConfig2W(service.get(), SERVICE_CONFIG_SERVICE_SID_INFO,
                            reinterpret_cast<BYTE *>(&oldSidInfo), sizeof(oldSidInfo), &bytes)) {
        error = GetLastError();
        return failedBeforeMutation(error);
    }
    AclTransaction acl;
    ConfigTransaction config;
    if (configure) {
        error = stopService(service.get());
        if (!error) error = config.replace(options.config, options.pendingConfig);
        if (!error) {
            SERVICE_SID_INFO sidInfo{SERVICE_SID_TYPE_UNRESTRICTED};
            if (!ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_SERVICE_SID_INFO, &sidInfo))
                error = GetLastError();
        }
        if (!error) error = grantServicePaths(options, acl);
        if (!error) {
            const auto command = serviceCommand(options);
            if (!ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE,
                options.enabled == L"1" ? SERVICE_AUTO_START : SERVICE_DISABLED,
                SERVICE_NO_CHANGE, command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
                error = GetLastError();
        }
        if (!error && options.enabled == L"1") error = startService(service.get());
    } else if (options.action == L"--enable") {
        if (!ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, SERVICE_AUTO_START,
            SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            error = GetLastError();
        if (!error) error = startService(service.get());
    } else if (options.action == L"--disable") {
        error = stopService(service.get());
        if (!error && !ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, SERVICE_DISABLED,
            SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            error = GetLastError();
    } else {
        error = ERROR_INVALID_PARAMETER;
    }
    if (!error && configure) error = removeLegacyTask(options.name);
    if (!error) {
        acl.commit();
        config.commit();
        return ERROR_SUCCESS;
    }
    // Attempt every restoration, but never report the ordinary operation error
    // as if rollback succeeded when ACL/config/SCM restoration also failed.
    DWORD rollbackError = ERROR_SUCCESS;
    const auto restored = [&](DWORD value) { if (value && !rollbackError) rollbackError = value; };
    restored(stopService(service.get()));
    restored(acl.rollback());
    restored(config.rollback());
    if (created) {
        if (!DeleteService(service.get())) restored(GetLastError());
    } else {
        if (!ChangeServiceConfigW(service.get(), SERVICE_NO_CHANGE, oldConfig->dwStartType,
            SERVICE_NO_CHANGE, oldConfig->lpBinaryPathName, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            restored(GetLastError());
        if (!ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_SERVICE_SID_INFO, &oldSidInfo))
            restored(GetLastError());
        if (wasRunning) restored(startService(service.get()));
    }
    return rollbackResult(error, rollbackError);
}

struct InsensitiveLess {
    bool operator()(const std::wstring &a, const std::wstring &b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
};

std::wstring systemPath()
{
    DWORD bytes = 0;
    constexpr auto key = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                     nullptr, nullptr, &bytes) == ERROR_SUCCESS && bytes) {
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, 0);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                         nullptr, buffer.data(), &bytes) == ERROR_SUCCESS) return buffer.data();
    }
    std::vector<wchar_t> directory(32768);
    const UINT length = GetSystemDirectoryW(directory.data(), UINT(directory.size()));
    return length && length < directory.size() ? std::wstring(directory.data(), length) : L"";
}

std::vector<wchar_t> workerEnvironment(const Options &options)
{
    std::map<std::wstring, std::wstring, InsensitiveLess> values;
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (const wchar_t *item = block; *item; item += wcslen(item) + 1) {
            const std::wstring entry(item);
            const size_t equals = entry.find(L'=', entry[0] == L'=' ? 1 : 0);
            if (equals != std::wstring::npos) values[entry.substr(0, equals)] = entry.substr(equals + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    std::wstring path = parentPath(options.worker);
    if (!options.runtimePath.empty()) path += L';' + options.runtimePath;
    path += L';' + systemPath();
    values[L"PATH"] = path;
    std::vector<wchar_t> result;
    for (const auto &entry : values) {
        const auto line = entry.first + L'=' + entry.second;
        result.insert(result.end(), line.begin(), line.end());
        result.push_back(0);
    }
    result.push_back(0);
    return result;
}

Options serviceOptions;
SERVICE_STATUS_HANDLE statusHandle = nullptr;
SRWLOCK statusLock = SRWLOCK_INIT;
SERVICE_STATUS currentStatus{};
HANDLE stopEvent = nullptr;

void replaceStopEvent(HANDLE event)
{
    AcquireSRWLockExclusive(&statusLock);
    stopEvent = event;
    ReleaseSRWLockExclusive(&statusLock);
}

void setStatus(DWORD state, DWORD error = ERROR_SUCCESS, DWORD hint = 0)
{
    AcquireSRWLockExclusive(&statusLock);
    currentStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    currentStatus.dwCurrentState = state;
    currentStatus.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    currentStatus.dwWin32ExitCode = error ? ERROR_SERVICE_SPECIFIC_ERROR : ERROR_SUCCESS;
    currentStatus.dwServiceSpecificExitCode = error;
    currentStatus.dwWaitHint = hint;
    currentStatus.dwCheckPoint = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING)
        ? currentStatus.dwCheckPoint + 1 : 0;
    SetServiceStatus(statusHandle, &currentStatus);
    ReleaseSRWLockExclusive(&statusLock);
}

DWORD WINAPI controlHandler(DWORD control, DWORD, LPVOID, LPVOID)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        setStatus(SERVICE_STOP_PENDING, ERROR_SUCCESS, 60000);
        AcquireSRWLockShared(&statusLock);
        if (stopEvent) SetEvent(stopEvent);
        ReleaseSRWLockShared(&statusLock);
        return NO_ERROR;
    }
    if (control == SERVICE_CONTROL_INTERROGATE) {
        AcquireSRWLockShared(&statusLock);
        SetServiceStatus(statusHandle, &currentStatus);
        ReleaseSRWLockShared(&statusLock);
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD runWorker()
{
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle event(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    if (!event.get()) return GetLastError();
    Handle readyEvent(CreateEventW(&attributes, TRUE, FALSE, nullptr));
    if (!readyEvent.get()) return GetLastError();
    replaceStopEvent(event.get());
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributeMemory(attributeBytes);
    auto *attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeMemory.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
        replaceStopEvent(nullptr);
        return GetLastError();
    }
    HANDLE inherited[] = {event.get(), readyEvent.get()};
    if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) {
        const DWORD error = GetLastError();
        DeleteProcThreadAttributeList(attributeList);
        replaceStopEvent(nullptr);
        return error;
    }
    std::wstring command = quote(serviceOptions.worker) + L" --history-service-worker --config "
        + quote(serviceOptions.config) + L" --stop-handle "
        + std::to_wstring(reinterpret_cast<ULONG_PTR>(event.get())) + L" --ready-handle "
        + std::to_wstring(reinterpret_cast<ULONG_PTR>(readyEvent.get()));
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);
    auto environment = workerEnvironment(serviceOptions);
    const auto workingDirectory = parentPath(serviceOptions.worker);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributeList;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(serviceOptions.worker.c_str(), mutableCommand.data(),
        nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        environment.data(), workingDirectory.c_str(), &startup.StartupInfo, &process);
    const DWORD startError = started ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(attributeList);
    if (!started) { replaceStopEvent(nullptr); return startError; }
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    // A job ensures that no unattended worker survives a crashed/killed host.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.get()) {
        const DWORD error = GetLastError();
        TerminateProcess(processHandle.get(), error);
        replaceStopEvent(nullptr);
        return error;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))
        || !AssignProcessToJobObject(job.get(), processHandle.get())) {
        const DWORD error = GetLastError();
        TerminateProcess(processHandle.get(), error);
        replaceStopEvent(nullptr);
        return error;
    }
    if (ResumeThread(threadHandle.get()) == DWORD(-1)) {
        const DWORD error = GetLastError();
        replaceStopEvent(nullptr);
        return error;
    }
    // Do not tell SCM that startup succeeded merely because CreateProcess did:
    // missing Qt DLLs, unreadable config and duplicate workers must roll back.
    HANDLE startupWait[] = {processHandle.get(), readyEvent.get()};
    const DWORD ready = WaitForMultipleObjects(2, startupWait, FALSE, 20000);
    if (ready != WAIT_OBJECT_0 + 1) {
        DWORD error = ready == WAIT_TIMEOUT ? ERROR_SERVICE_REQUEST_TIMEOUT : GetLastError();
        if (ready == WAIT_OBJECT_0) {
            if (!GetExitCodeProcess(processHandle.get(), &error)) error = GetLastError();
            if (!error) error = ERROR_PROCESS_ABORTED;
        }
        replaceStopEvent(nullptr);
        return error; // The job kills a hung startup worker before SCM reports STOPPED.
    }
    setStatus(SERVICE_RUNNING);
    HANDLE waiting[] = {event.get(), processHandle.get()};
    const DWORD signaled = WaitForMultipleObjects(2, waiting, FALSE, INFINITE);
    DWORD exitCode = ERROR_SUCCESS;
    if (signaled == WAIT_OBJECT_0) {
        // A network request has a 30-second timeout. Leave enough time to exit
        // that request, write the cancellation audit and release SQLite locks.
        for (unsigned int attempt = 0; attempt < 45; ++attempt) {
            if (WaitForSingleObject(processHandle.get(), 1000) == WAIT_OBJECT_0) break;
            setStatus(SERVICE_STOP_PENDING, ERROR_SUCCESS, 60000 - attempt * 1000);
        }
        if (WaitForSingleObject(processHandle.get(), 0) != WAIT_OBJECT_0) {
            TerminateProcess(processHandle.get(), ERROR_TIMEOUT);
            WaitForSingleObject(processHandle.get(), 5000);
            exitCode = ERROR_TIMEOUT;
        }
    } else if (signaled == WAIT_OBJECT_0 + 1) {
        if (!GetExitCodeProcess(processHandle.get(), &exitCode)) exitCode = GetLastError();
        if (!exitCode && WaitForSingleObject(event.get(), 0) != WAIT_OBJECT_0) exitCode = ERROR_PROCESS_ABORTED;
    } else {
        exitCode = GetLastError();
        TerminateProcess(processHandle.get(), exitCode);
        WaitForSingleObject(processHandle.get(), 5000);
    }
    replaceStopEvent(nullptr);
    return exitCode;
}

void WINAPI serviceMain(DWORD, LPWSTR *)
{
    statusHandle = RegisterServiceCtrlHandlerExW(serviceOptions.name.c_str(), controlHandler, nullptr);
    if (!statusHandle) return;
    setStatus(SERVICE_START_PENDING, ERROR_SUCCESS, 30000);
    const DWORD error = runWorker();
    setStatus(SERVICE_STOPPED, error);
}
} // namespace

int main()
{
    Options options;
    DWORD error = parseOptions(options);
    if (!error) {
        if (options.action == L"--query") error = query(options);
        else if (options.action == L"--run") {
            serviceOptions = options;
            SERVICE_TABLE_ENTRYW table[] = {
                {const_cast<wchar_t *>(serviceOptions.name.c_str()), serviceMain}, {nullptr, nullptr}};
            if (!StartServiceCtrlDispatcherW(table)) error = GetLastError();
        } else error = mutate(options);
    }
    if (error) std::fprintf(stderr, "Windows service operation failed: %lu\n", static_cast<unsigned long>(error));
    return static_cast<int>(error);
}
