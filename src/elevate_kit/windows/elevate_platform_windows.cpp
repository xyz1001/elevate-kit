#include "../elevate_platform.h"

#include "../application_id.h"
#include "../task_registry.h"

#include <windows.h>

#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <taskschd.h>

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace elevate_kit::detail {
namespace {

constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\ElevateKit-";
constexpr auto kIoTimeout = std::chrono::seconds(3);

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() {
        reset();
    }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T **put() {
        reset();
        return &value_;
    }
    T *get() const {
        return value_;
    }
    T *operator->() const {
        return value_;
    }
    void reset() {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T *value_ = nullptr;
};

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() {
        reset();
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    HANDLE get() const {
        return value_;
    }
    void reset(HANDLE value = nullptr) {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }
    explicit operator bool() const {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_ = nullptr;
};

class ComApartment {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        owned_ = SUCCEEDED(result_);
        usable_ = owned_ || result_ == RPC_E_CHANGED_MODE;
    }
    ~ComApartment() {
        if (owned_) {
            CoUninitialize();
        }
    }
    bool ready() const {
        return usable_;
    }

private:
    HRESULT result_;
    bool owned_ = false;
    bool usable_ = false;
};

void reportTaskSchedulerFailure(const wchar_t *stage, HRESULT result) {
    wchar_t message[256]{};
    swprintf_s(message,
               L"ElevateKit Task Scheduler %ls failed: HRESULT=0x%08lX\n",
               stage, static_cast<unsigned long>(result));
    OutputDebugStringW(message);
    std::string stage_text;
    for (const wchar_t *character = stage; *character != L'\0'; ++character) {
        stage_text.push_back(static_cast<char>(*character));
    }
    fmt::print(stderr, "Task Scheduler {} failed: HRESULT=0x{:08X}\n",
               stage_text, static_cast<unsigned long>(result));
}

void platformLog(const char *message) {
    fmt::print(stderr, "elevate-kit: {}\n", message);
}

std::string narrow(const std::wstring &value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr,
                                         0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size,
                            nullptr, nullptr) == 0) {
        return {};
    }
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

bool taskSchedulerSucceeded(const wchar_t *stage, HRESULT result) {
    if (FAILED(result)) {
        reportTaskSchedulerFailure(stage, result);
        return false;
    }
    return true;
}

std::chrono::steady_clock::time_point deadline() {
    return std::chrono::steady_clock::now() + kIoTimeout;
}

DWORD remaining(std::chrono::steady_clock::time_point end) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= end) {
        return 0;
    }
    const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - now)
                    .count();
    return static_cast<DWORD>(milliseconds == 0 ? 1 : milliseconds);
}

void cancelAndObserve(HANDLE pipe, OVERLAPPED &overlapped) {
    CancelIoEx(pipe, &overlapped);
    DWORD transferred = 0;
    GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
}

bool getPath(HANDLE process, std::wstring &path) {
    DWORD size = 32768;
    std::wstring value(size, L'\0');
    if (!QueryFullProcessImageNameW(process, 0, value.data(), &size)) {
        return false;
    }
    value.resize(size);
    DWORD full_size = GetFullPathNameW(value.c_str(), 0, nullptr, nullptr);
    if (full_size == 0) {
        return false;
    }
    path.resize(full_size);
    if (GetFullPathNameW(value.c_str(), full_size, path.data(), nullptr) == 0) {
        return false;
    }
    path.resize(wcslen(path.c_str()));
    return true;
}

bool currentHost(std::wstring &host, std::wstring &working_directory) {
    DWORD host_size = MAX_PATH;
    std::wstring host_buffer(host_size, L'\0');
    DWORD copied = GetModuleFileNameW(nullptr, host_buffer.data(), host_size);
    if (copied == 0) {
        return false;
    }
    host_buffer.resize(copied);

    DWORD full_size =
            GetFullPathNameW(host_buffer.c_str(), 0, nullptr, nullptr);
    if (full_size == 0) {
        return false;
    }
    std::wstring full_host(full_size, L'\0');
    if (GetFullPathNameW(host_buffer.c_str(), full_size, full_host.data(),
                         nullptr) == 0) {
        return false;
    }
    full_host.resize(wcslen(full_host.c_str()));

    const std::size_t separator = full_host.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return false;
    }
    std::wstring directory = full_host.substr(0, separator);
    if (separator == 2 && full_host[1] == L':') {
        directory.push_back(L'\\');
    }
    host = std::move(full_host);
    working_directory = std::move(directory);
    return true;
}

std::wstring moduleFileName() {
    DWORD size = MAX_PATH;
    for (;;) {
        std::vector<wchar_t> buffer(size, L'\0');
        const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), size);
        if (copied == 0) {
            return {};
        }
        if (copied < size - 1) {
            const std::wstring path(buffer.data(), copied);
            const std::size_t separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? path
                                                   : path.substr(separator + 1);
        }
        size *= 2;
    }
}

bool isTaskNameCharacter(unsigned char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '.' ||
           character == '-';
}

std::wstring taskName() {
    std::wstring source;
    const std::string &configured = configuredApplicationId();
    if (configured.empty()) {
        source = moduleFileName();
        if (source.size() >= 4 &&
            _wcsicmp(source.c_str() + source.size() - 4, L".exe") == 0) {
            source.resize(source.size() - 4);
        }
    }

    std::wstring normalized;
    if (configured.empty()) {
        for (wchar_t character : source) {
            normalized.push_back(
                    character <= 0x7f && isTaskNameCharacter(
                                                 static_cast<unsigned char>(
                                                         character))
                            ? character
                            : L'-');
        }
    } else {
        for (unsigned char character : configured) {
            normalized.push_back(isTaskNameCharacter(character)
                                         ? static_cast<wchar_t>(character)
                                         : L'-');
        }
    }
    if (normalized.empty()) {
        normalized = L"host";
    }
    return std::wstring(L"ElevateKit.") + normalized;
}

bool releaseHostIsProtected(const std::wstring &host) {
#ifdef NDEBUG
    PWSTR known_folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT,
                                    nullptr, &known_folder))) {
        return false;
    }
    const std::wstring program_files(known_folder);
    CoTaskMemFree(known_folder);
    if (host.size() <= program_files.size() ||
        _wcsnicmp(host.c_str(), program_files.c_str(), program_files.size()) !=
                0) {
        return false;
    }
    return host[program_files.size()] == L'\\' ||
           host[program_files.size()] == L'/';
#else
    (void) host;
    return true;
#endif
}

bool currentTokenElevated() {
    Handle token;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return false;
    }
    token.reset(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation,
                               sizeof(elevation), &size) != FALSE &&
           elevation.TokenIsElevated != 0;
}

bool currentUserSid(std::wstring &sid, bool require_administrator) {
    Handle token;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return false;
    }
    token.reset(raw);

    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user_buffer(size);
    if (size == 0 || !GetTokenInformation(token.get(), TokenUser,
                                          user_buffer.data(), size, &size)) {
        return false;
    }
    LPWSTR sid_string = nullptr;
    if (!ConvertSidToStringSidW(static_cast<PTOKEN_USER>(
                                        static_cast<void *>(user_buffer.data()))
                                        ->User.Sid,
                                &sid_string)) {
        return false;
    }
    sid = sid_string;
    LocalFree(sid_string);

    if (!require_administrator) {
        return true;
    }
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &administrators)) {
        return false;
    }
    DWORD groups_size = 0;
    SetLastError(ERROR_SUCCESS);
    const BOOL sizing_result = GetTokenInformation(token.get(), TokenGroups,
                                                   nullptr, 0, &groups_size);
    const DWORD sizing_error = GetLastError();
    if (sizing_result == FALSE && sizing_error != ERROR_INSUFFICIENT_BUFFER) {
        const DWORD error = sizing_error;
        fmt::print(stderr, "elevate-kit: TokenGroups sizing failed, error={}\n",
                   error);
        FreeSid(administrators);
        return false;
    }
    std::vector<unsigned char> groups_buffer(groups_size);
    if (groups_size == 0 ||
        !GetTokenInformation(token.get(), TokenGroups, groups_buffer.data(),
                             groups_size, &groups_size)) {
        const DWORD error = GetLastError();
        fmt::print(stderr, "elevate-kit: TokenGroups query failed, error={}\n",
                   error);
        FreeSid(administrators);
        return false;
    }
    const auto *groups =
            reinterpret_cast<const TOKEN_GROUPS *>(groups_buffer.data());
    bool member = false;
    for (DWORD index = 0; index < groups->GroupCount; ++index) {
        if (EqualSid(groups->Groups[index].Sid, administrators) != FALSE) {
            member = true;
            const DWORD attributes = groups->Groups[index].Attributes;
            fmt::print(stderr,
                       "elevate-kit: administrator SID present=true "
                       "attributes=0x{:08X} "
                       "deny_only={}\n",
                       attributes,
                       (attributes & SE_GROUP_USE_FOR_DENY_ONLY) != 0);
            break;
        }
    }
    if (!member) {
        platformLog("administrator SID present=false");
    }
    FreeSid(administrators);
    return member;
}

bool accountNamesForSid(const std::wstring &sid, std::wstring &account,
                        std::wstring &qualified_account) {
    PSID sid_value = nullptr;
    if (!ConvertStringSidToSidW(sid.c_str(), &sid_value)) {
        return false;
    }

    DWORD account_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE sid_type{};
    SetLastError(ERROR_SUCCESS);
    LookupAccountSidW(nullptr, sid_value, nullptr, &account_size, nullptr,
                      &domain_size, &sid_type);
    const DWORD sizing_error = GetLastError();
    if (sizing_error != ERROR_INSUFFICIENT_BUFFER || account_size == 0) {
        LocalFree(sid_value);
        return false;
    }

    std::vector<wchar_t> account_buffer(account_size);
    std::vector<wchar_t> domain_buffer(domain_size == 0 ? 1 : domain_size);
    if (!LookupAccountSidW(nullptr, sid_value, account_buffer.data(),
                           &account_size, domain_buffer.data(), &domain_size,
                           &sid_type)) {
        LocalFree(sid_value);
        return false;
    }
    LocalFree(sid_value);

    account.assign(account_buffer.data());
    if (domain_size != 0 && domain_buffer[0] != L'\0') {
        qualified_account.assign(domain_buffer.data());
        qualified_account += L"\\";
        qualified_account += account;
    } else {
        qualified_account = account;
    }
    return true;
}

bool processTokenElevated(DWORD process_id) {
    Handle process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (!process) {
        return false;
    }
    Handle token;
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process.get(), TOKEN_QUERY, &raw)) {
        return false;
    }
    token.reset(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation,
                               sizeof(elevation), &size) != FALSE &&
           elevation.TokenIsElevated != 0;
}

bool processIsHost(DWORD process_id, const std::wstring &host) {
    Handle process(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (!process) {
        return false;
    }
    std::wstring image;
    if (!getPath(process.get(), image)) {
        return false;
    }
    return _wcsicmp(image.c_str(), host.c_str()) == 0;
}

std::wstring pipeName(const std::string &id) {
    std::wstring wide(id.begin(), id.end());
    return std::wstring(kPipePrefix) + wide;
}

bool readExact(HANDLE pipe, void *data, DWORD size,
               std::chrono::steady_clock::time_point end) {
    auto *bytes = static_cast<unsigned char *>(data);
    DWORD offset = 0;
    while (offset < size) {
        OVERLAPPED overlapped{};
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) {
            return false;
        }
        overlapped.hEvent = event.get();
        DWORD transferred = 0;
        if (ReadFile(pipe, bytes + offset, size - offset, &transferred,
                     &overlapped)) {
            if (transferred == 0) {
                return false;
            }
        } else if (GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(event.get(), remaining(end)) !=
                WAIT_OBJECT_0) {
                cancelAndObserve(pipe, overlapped);
                return false;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &transferred, FALSE)) {
                cancelAndObserve(pipe, overlapped);
                return false;
            }
            if (transferred == 0) {
                return false;
            }
        } else {
            return false;
        }
        offset += transferred;
    }
    return true;
}

bool writeExact(HANDLE pipe, const void *data, DWORD size,
                std::chrono::steady_clock::time_point end) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    DWORD offset = 0;
    while (offset < size) {
        OVERLAPPED overlapped{};
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) {
            return false;
        }
        overlapped.hEvent = event.get();
        DWORD transferred = 0;
        if (WriteFile(pipe, bytes + offset, size - offset, &transferred,
                      &overlapped)) {
            if (transferred == 0) {
                return false;
            }
        } else if (GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(event.get(), remaining(end)) !=
                WAIT_OBJECT_0) {
                cancelAndObserve(pipe, overlapped);
                return false;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &transferred, FALSE)) {
                cancelAndObserve(pipe, overlapped);
                return false;
            }
            if (transferred == 0) {
                return false;
            }
        } else {
            return false;
        }
        offset += transferred;
    }
    return true;
}

bool readFrame(HANDLE pipe, std::string &payload,
               std::chrono::steady_clock::time_point end) {
    std::array<std::uint8_t, 4> header{};
    if (!readExact(pipe, header.data(), static_cast<DWORD>(header.size()),
                   end)) {
        return false;
    }
    payload.resize(decodePayloadLength(header));
    return readExact(pipe, payload.data(), static_cast<DWORD>(payload.size()),
                     end);
}

bool writeFrame(HANDLE pipe, const std::string &payload,
                std::chrono::steady_clock::time_point end) {
    if (payload.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    const auto header =
            encodePayloadLength(static_cast<std::uint32_t>(payload.size()));
    return writeExact(pipe, header.data(), static_cast<DWORD>(header.size()),
                      end) &&
           writeExact(pipe, payload.data(), static_cast<DWORD>(payload.size()),
                      end);
}

bool connectPipe(HANDLE pipe, std::chrono::steady_clock::time_point end) {
    OVERLAPPED overlapped{};
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) {
        return false;
    }
    overlapped.hEvent = event.get();
    if (ConnectNamedPipe(pipe, &overlapped)) {
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) {
        return true;
    }
    if (error != ERROR_IO_PENDING) {
        return false;
    }
    if (WaitForSingleObject(event.get(), remaining(end)) != WAIT_OBJECT_0) {
        cancelAndObserve(pipe, overlapped);
        return false;
    }
    DWORD transferred = 0;
    if (GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) == FALSE) {
        cancelAndObserve(pipe, overlapped);
        return false;
    }
    return true;
}

bool openService(ComPtr<ITaskService> &service) {
    const HRESULT create_result =
            CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(service.put()));
    if (FAILED(create_result)) {
        reportTaskSchedulerFailure(L"CoCreateInstance", create_result);
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    const HRESULT connect_result = service->Connect(empty, empty, empty, empty);
    if (FAILED(connect_result)) {
        reportTaskSchedulerFailure(L"ITaskService::Connect", connect_result);
        return false;
    }
    return true;
}

bool makeRunArguments(const std::string &id, VARIANT &arguments) {
    VariantInit(&arguments);
    SAFEARRAY *values = SafeArrayCreateVector(VT_BSTR, 0, 1);
    if (values == nullptr) {
        return false;
    }
    std::wstring wide(id.begin(), id.end());
    BSTR value = SysAllocString(wide.c_str());
    LONG index = 0;
    if (value == nullptr) {
        SafeArrayDestroy(values);
        return false;
    }
    const HRESULT result = SafeArrayPutElement(values, &index, value);
    SysFreeString(value);
    if (FAILED(result)) {
        SafeArrayDestroy(values);
        return false;
    }
    arguments.vt = VT_ARRAY | VT_BSTR;
    arguments.parray = values;
    return true;
}

bool runScheduledWorker(const std::string &id) {
    ComApartment apartment;
    if (!apartment.ready()) {
        return false;
    }
    ComPtr<ITaskService> service;
    if (!openService(service)) {
        return false;
    }
    ComPtr<ITaskFolder> folder;
    const std::wstring registered_task_name = taskName();
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    BSTR root = SysAllocString(L"\\");
    const HRESULT folder_result =
            root == nullptr ? E_OUTOFMEMORY
                            : service->GetFolder(root, folder.put());
    SysFreeString(root);
    if (task_name == nullptr || FAILED(folder_result)) {
        SysFreeString(task_name);
        return false;
    }
    ComPtr<IRegisteredTask> task;
    const HRESULT task_result = folder->GetTask(task_name, task.put());
    SysFreeString(task_name);
    if (FAILED(task_result)) {
        return false;
    }
    VARIANT arguments;
    if (!makeRunArguments(id, arguments)) {
        return false;
    }
    ComPtr<IRunningTask> running;
    const HRESULT result = task->RunEx(arguments, 0, 0, nullptr, running.put());
    VariantClear(&arguments);
    if (FAILED(result)) {
        reportTaskSchedulerFailure(L"IRegisteredTask::RunEx", result);
    }
    return SUCCEEDED(result);
}

bool installTask(const std::wstring &host,
                 const std::wstring &working_directory,
                 const std::wstring &user_sid) {
    platformLog("install: connecting to Task Scheduler");
    ComPtr<ITaskService> service;
    if (!openService(service)) {
        return false;
    }
    ComPtr<ITaskFolder> folder;
    BSTR root = SysAllocString(L"\\");
    const HRESULT folder_result =
            root == nullptr ? E_OUTOFMEMORY
                            : service->GetFolder(root, folder.put());
    SysFreeString(root);
    if (!taskSchedulerSucceeded(L"GetFolder", folder_result)) {
        return false;
    }
    ComPtr<ITaskDefinition> definition;
    const HRESULT new_task_result = service->NewTask(0, definition.put());
    if (!taskSchedulerSucceeded(L"NewTask", new_task_result)) {
        return false;
    }
    ComPtr<IPrincipal> principal;
    ComPtr<ITaskSettings> settings;
    BSTR user_id = SysAllocString(user_sid.c_str());
    const HRESULT principal_get_result =
            definition->get_Principal(principal.put());
    if (user_id == nullptr ||
        !taskSchedulerSucceeded(L"get_Principal", principal_get_result)) {
        SysFreeString(user_id);
        return false;
    }
    const HRESULT principal_result = principal->put_UserId(user_id);
    SysFreeString(user_id);
    if (!taskSchedulerSucceeded(L"put_UserId", principal_result)) {
        return false;
    }
    const HRESULT settings_result = definition->get_Settings(settings.put());
    const HRESULT logon_result =
            principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    const HRESULT run_level_result =
            principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    const HRESULT demand_start_result =
            SUCCEEDED(settings_result)
                    ? settings->put_AllowDemandStart(VARIANT_TRUE)
                    : E_FAIL;
    if (!taskSchedulerSucceeded(L"get_Settings", settings_result) ||
        !taskSchedulerSucceeded(L"put_LogonType", logon_result) ||
        !taskSchedulerSucceeded(L"put_RunLevel", run_level_result) ||
        !taskSchedulerSucceeded(L"put_AllowDemandStart", demand_start_result)) {
        return false;
    }
    ComPtr<IActionCollection> actions;
    const HRESULT actions_result = definition->get_Actions(actions.put());
    if (!taskSchedulerSucceeded(L"get_Actions", actions_result)) {
        return false;
    }
    ComPtr<IAction> action;
    const HRESULT create_action_result =
            actions->Create(TASK_ACTION_EXEC, action.put());
    if (!taskSchedulerSucceeded(L"Actions::Create", create_action_result)) {
        return false;
    }
    ComPtr<IExecAction> exec;
    const HRESULT query_action_result =
            action->QueryInterface(IID_PPV_ARGS(exec.put()));
    if (!taskSchedulerSucceeded(L"QueryInterface(IExecAction)",
                                query_action_result)) {
        return false;
    }
    BSTR path = SysAllocString(host.c_str());
    BSTR directory = SysAllocString(working_directory.c_str());
    BSTR arguments = SysAllocString(L"--elevated --elevate-ipc $(Arg0)");
    const bool allocated =
            path != nullptr && directory != nullptr && arguments != nullptr;
    const HRESULT action_result =
            allocated ? exec->put_Path(path) : E_OUTOFMEMORY;
    const HRESULT directory_result =
            allocated ? exec->put_WorkingDirectory(directory) : E_OUTOFMEMORY;
    const HRESULT arguments_result =
            allocated ? exec->put_Arguments(arguments) : E_OUTOFMEMORY;
    SysFreeString(path);
    SysFreeString(directory);
    SysFreeString(arguments);
    if (!taskSchedulerSucceeded(L"put_Path", action_result) ||
        !taskSchedulerSucceeded(L"put_WorkingDirectory", directory_result) ||
        !taskSchedulerSucceeded(L"put_Arguments", arguments_result)) {
        return false;
    }

    VARIANT empty;
    VariantInit(&empty);
    const std::wstring registered_task_name = taskName();
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    if (task_name == nullptr) {
        return false;
    }
    ComPtr<IRegisteredTask> registered;
    const HRESULT result = folder->RegisterTaskDefinition(
            task_name, definition.get(), TASK_CREATE_OR_UPDATE, empty, empty,
            TASK_LOGON_INTERACTIVE_TOKEN, empty, registered.put());
    SysFreeString(task_name);
    if (FAILED(result)) {
        reportTaskSchedulerFailure(L"RegisterTaskDefinition", result);
    } else {
        platformLog("install: Task Scheduler registration succeeded");
    }
    return SUCCEEDED(result);
}

bool taskMatches(const std::wstring &host,
                 const std::wstring &working_directory,
                 const std::wstring &user_sid) {
    const std::wstring registered_task_name = taskName();
    const auto mismatch = [&registered_task_name](const char *field) {
        fmt::print(stderr, "elevate-kit: readiness task '{}' mismatch: {}\n",
                   narrow(registered_task_name), field);
    };
    std::wstring account;
    std::wstring qualified_account;
    if (!accountNamesForSid(user_sid, account, qualified_account)) {
        mismatch("current user account lookup");
        return false;
    }
    ComPtr<ITaskService> service;
    if (!openService(service)) {
        mismatch("Task Scheduler service");
        return false;
    }
    ComPtr<ITaskFolder> folder;
    BSTR root = SysAllocString(L"\\");
    const HRESULT folder_result =
            root == nullptr ? E_OUTOFMEMORY
                            : service->GetFolder(root, folder.put());
    SysFreeString(root);
    if (FAILED(folder_result)) {
        mismatch("task folder");
        return false;
    }
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    if (task_name == nullptr) {
        return false;
    }
    ComPtr<IRegisteredTask> task;
    const HRESULT task_result = folder->GetTask(task_name, task.put());
    SysFreeString(task_name);
    if (FAILED(task_result)) {
        mismatch("task missing");
        return false;
    }
    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(task->get_Enabled(&enabled)) || enabled != VARIANT_TRUE) {
        mismatch("enabled");
        return false;
    }
    ComPtr<ITaskDefinition> definition;
    ComPtr<IPrincipal> principal;
    ComPtr<ITaskSettings> settings;
    if (FAILED(task->get_Definition(definition.put())) ||
        FAILED(definition->get_Principal(principal.put())) ||
        FAILED(definition->get_Settings(settings.put()))) {
        mismatch("definition");
        return false;
    }
    TASK_LOGON_TYPE logon_type;
    TASK_RUNLEVEL_TYPE run_level;
    VARIANT_BOOL allow_demand = VARIANT_FALSE;
    BSTR principal_user = nullptr;
    if (FAILED(principal->get_LogonType(&logon_type)) ||
        FAILED(principal->get_RunLevel(&run_level)) ||
        FAILED(principal->get_UserId(&principal_user)) ||
        FAILED(settings->get_AllowDemandStart(&allow_demand)) ||
        principal_user == nullptr) {
        SysFreeString(principal_user);
        mismatch("principal properties");
        return false;
    }
    const bool principal_matches =
            _wcsicmp(principal_user, account.c_str()) == 0 ||
            _wcsicmp(principal_user, qualified_account.c_str()) == 0;
    fmt::print(stderr,
               "elevate-kit: readiness principal UserId='{}', expected "
               "account='{}', "
               "qualified='{}'\n",
               narrow(std::wstring(principal_user)), narrow(account),
               narrow(qualified_account));
    if (!principal_matches) {
        SysFreeString(principal_user);
        mismatch("principal UserId");
        return false;
    }
    if (logon_type != TASK_LOGON_INTERACTIVE_TOKEN) {
        SysFreeString(principal_user);
        mismatch("principal logon type");
        return false;
    }
    if (run_level != TASK_RUNLEVEL_HIGHEST) {
        SysFreeString(principal_user);
        mismatch("principal run level");
        return false;
    }
    if (allow_demand != VARIANT_TRUE) {
        SysFreeString(principal_user);
        mismatch("allow demand start");
        return false;
    }
    SysFreeString(principal_user);
    ComPtr<IActionCollection> actions;
    LONG count = 0;
    if (FAILED(definition->get_Actions(actions.put())) ||
        FAILED(actions->get_Count(&count)) || count != 1) {
        mismatch("actions");
        return false;
    }
    ComPtr<IAction> action;
    if (FAILED(actions->get_Item(1, action.put()))) {
        mismatch("action");
        return false;
    }
    TASK_ACTION_TYPE action_type;
    if (FAILED(action->get_Type(&action_type)) ||
        action_type != TASK_ACTION_EXEC) {
        mismatch("action type");
        return false;
    }
    ComPtr<IExecAction> exec;
    if (FAILED(action->QueryInterface(IID_PPV_ARGS(exec.put())))) {
        mismatch("exec action");
        return false;
    }
    BSTR path = nullptr;
    BSTR directory = nullptr;
    BSTR arguments = nullptr;
    const HRESULT path_result = exec->get_Path(&path);
    const HRESULT directory_result = exec->get_WorkingDirectory(&directory);
    const HRESULT arguments_result = exec->get_Arguments(&arguments);
    const bool matches =
            SUCCEEDED(path_result) && SUCCEEDED(directory_result) &&
            SUCCEEDED(arguments_result) && path != nullptr &&
            directory != nullptr && arguments != nullptr &&
            _wcsicmp(path, host.c_str()) == 0 &&
            _wcsicmp(directory, working_directory.c_str()) == 0 &&
            wcscmp(arguments, L"--elevated --elevate-ipc $(Arg0)") == 0;
    SysFreeString(path);
    SysFreeString(directory);
    SysFreeString(arguments);
    if (!matches) {
        mismatch("action path, working directory, or arguments");
    }
    return matches;
}

bool makePipe(const std::wstring &name, Handle &pipe) {
    pipe.reset(CreateNamedPipeW(name.c_str(),
                                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                        FILE_FLAG_FIRST_PIPE_INSTANCE,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                        PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                1, 0, 0, 0, nullptr));
    return static_cast<bool>(pipe);
}

}  // namespace

bool installOrRepair() {
    platformLog("install: checking elevated administrator token");
    std::wstring user_sid;
    if (!currentTokenElevated() || !currentUserSid(user_sid, true)) {
        platformLog("install: elevated administrator check failed");
        return false;
    }
    std::wstring host;
    std::wstring working_directory;
    if (!currentHost(host, working_directory) ||
        !releaseHostIsProtected(host)) {
        platformLog("install: host path check failed");
        return false;
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        platformLog("install: COM apartment unavailable");
        return false;
    }
    return installTask(host, working_directory, user_sid);
}

bool isPasswordlessReady() {
    platformLog("readiness: checking");
    std::wstring user_sid;
    if (!currentUserSid(user_sid, true)) {
        platformLog("readiness: administrator check failed");
        return false;
    }
    std::wstring host;
    std::wstring working_directory;
    if (!currentHost(host, working_directory) ||
        !releaseHostIsProtected(host)) {
        platformLog("readiness: host path check failed");
        return false;
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        platformLog("readiness: COM apartment unavailable");
        return false;
    }
    const bool ready = taskMatches(host, working_directory, user_sid);
    platformLog(ready ? "readiness: ready"
                      : "readiness: task mismatch or missing");
    return ready;
}

bool executeRequest(const Request &request) {
    std::wstring user_sid;
    if (!currentUserSid(user_sid, true)) {
        platformLog("executeRequest: administrator check failed");
        return false;
    }
    const bool ready = isPasswordlessReady();
    fmt::print(stderr, "elevate-kit: executeRequest readiness={}\n", ready);
    if (!ready) {
        platformLog("executeRequest: repair required");
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS;
        execute.lpVerb = L"runas";
        std::wstring host;
        std::wstring working_directory;
        if (!currentHost(host, working_directory)) {
            return false;
        }
        execute.lpFile = host.c_str();
        execute.lpParameters = L"--elevated-install";
        execute.lpDirectory = working_directory.c_str();
        platformLog("executeRequest: requesting UAC repair");
        if (!ShellExecuteExW(&execute)) {
            fmt::print(stderr, "elevate-kit: ShellExecuteEx failed, error={}\n",
                       GetLastError());
            return false;
        }
        Handle process(execute.hProcess);
        if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0) {
            platformLog("executeRequest: installer wait failed");
            return false;
        }
        DWORD exit_code = 1;
        if (!GetExitCodeProcess(process.get(), &exit_code)) {
            fmt::print(
                    stderr,
                    "elevate-kit: installer exit-code query failed, error={}\n",
                    GetLastError());
            return false;
        }
        fmt::print(stderr, "elevate-kit: installer exit code={}\n", exit_code);
        const bool repaired = isPasswordlessReady();
        fmt::print(stderr, "elevate-kit: repair readiness={}\n", repaired);
        if (exit_code != 0 || !repaired) {
            return false;
        }
    } else {
        platformLog("executeRequest: existing task is ready");
    }

    GUID guid{};
    wchar_t guid_buffer[39]{};
    if (FAILED(CoCreateGuid(&guid)) ||
        StringFromGUID2(guid, guid_buffer, 39) == 0) {
        return false;
    }
    const std::wstring id_wide(guid_buffer);
    const std::string id(id_wide.begin(), id_wide.end());
    const std::wstring name = pipeName(id);
    Handle pipe;
    if (!makePipe(name, pipe) || !runScheduledWorker(id)) {
        platformLog("executeRequest: pipe creation or RunEx failed");
        return false;
    }
    platformLog("executeRequest: pipe created and RunEx succeeded");
    const auto end = deadline();
    if (!connectPipe(pipe.get(), end)) {
        platformLog("executeRequest: worker connection failed");
        return false;
    }
    platformLog("executeRequest: worker connected");
    DWORD client_pid = 0;
    std::wstring host;
    std::wstring working_directory;
    if (!GetNamedPipeClientProcessId(pipe.get(), &client_pid) ||
        !currentHost(host, working_directory) ||
        !processIsHost(client_pid, host) || !processTokenElevated(client_pid)) {
        platformLog("executeRequest: worker peer validation failed");
        return false;
    }
    platformLog("executeRequest: worker peer validated");
    Request transmitted = request;
    transmitted.request_id = id;
    std::string payload;
    if (!serializeRequest(transmitted, payload) ||
        !writeFrame(pipe.get(), payload, end)) {
        platformLog("executeRequest: request write failed");
        return false;
    }
    platformLog("executeRequest: request sent");
    std::string response_payload;
    bool success = false;
    if (!readFrame(pipe.get(), response_payload, end) ||
        !deserializeResponse(response_payload, success)) {
        platformLog("executeRequest: response read or parse failed");
        return false;
    }
    fmt::print(stderr, "elevate-kit: response success={}\n", success);
    return success;
}

bool executeWorker(const std::string &ipc_id) {
    std::wstring user_sid;
    if (ipc_id.empty() || !currentTokenElevated() ||
        !currentUserSid(user_sid, true)) {
        return false;
    }
    std::wstring host;
    std::wstring working_directory;
    if (!currentHost(host, working_directory)) {
        return false;
    }
    const auto end = deadline();
    const std::wstring name = pipeName(ipc_id);
    if (!WaitNamedPipeW(name.c_str(), remaining(end))) {
        return false;
    }
    Handle pipe(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                            nullptr));
    if (!pipe) {
        return false;
    }
    DWORD server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe.get(), &server_pid) ||
        !processIsHost(server_pid, host)) {
        return false;
    }
    std::string payload;
    Request request;
    if (!readFrame(pipe.get(), payload, end) ||
        !deserializeRequest(payload, request) || request.request_id != ipc_id) {
        return false;
    }
    const bool success = runTask(request.task_name, request.params_json);
    std::string response;
    return serializeResponse(success, response) &&
           writeFrame(pipe.get(), response, end);
}

}  // namespace elevate_kit::detail
