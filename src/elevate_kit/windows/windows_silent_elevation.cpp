#include "../logger.h"
#include "../silent_elevation.h"
#include "common.h"
#include "templates.h"

#include <cstdint>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <nonstd/scope.hpp>
#ifdef SendMessage
#undef SendMessage
#endif
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <taskschd.h>
// clang-format on

#include <fmt/format.h>
#include <fmt/xchar.h>

#include "elevate_kit/application_id.h"

namespace elevate_kit {
namespace {

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
    bool Ready() const {
        return usable_;
    }

private:
    HRESULT result_;
    bool owned_ = false;
    bool usable_ = false;
};

std::wstring TaskName(const std::string &application_id) {
    return fmt::format(L"{}.ElevateKit", Utf8ToWide(application_id));
}

bool TaskSchedulerSucceeded(const wchar_t *stage, HRESULT result) {
    if (FAILED(result)) {
        std::wstring message = fmt::format(
                L"ElevateKit Task Scheduler {} failed: HRESULT=0x{:08X}\n",
                stage, static_cast<unsigned long>(result));
        OutputDebugStringW(message.c_str());
        std::string stage_text;
        for (const wchar_t *character = stage; *character != L'\0';
             ++character) {
            stage_text.push_back(static_cast<char>(*character));
        }
        LogError("Task Scheduler {} failed: HRESULT=0x{:08X}", stage_text,
                 static_cast<unsigned long>(result));
        return false;
    }
    return true;
}

std::optional<std::pair<std::wstring, std::wstring>>
CurrentExecutablePathAndDirectory() {
    DWORD host_size = MAX_PATH;
    std::wstring host_buffer(host_size, L'\0');
    DWORD copied = GetModuleFileNameW(nullptr, host_buffer.data(), host_size);
    if (copied == 0) {
        return std::nullopt;
    }
    host_buffer.resize(copied);

    DWORD full_size =
            GetFullPathNameW(host_buffer.c_str(), 0, nullptr, nullptr);
    if (full_size == 0) {
        return std::nullopt;
    }
    std::wstring full_host(full_size, L'\0');
    if (GetFullPathNameW(host_buffer.c_str(), full_size, full_host.data(),
                         nullptr) == 0) {
        return std::nullopt;
    }
    full_host.resize(wcslen(full_host.c_str()));

    std::size_t separator = full_host.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return std::nullopt;
    }
    std::wstring directory = full_host.substr(0, separator);
    if (separator == 2 && full_host[1] == L':') {
        directory.push_back(L'\\');
    }
    return std::make_pair(std::move(full_host), std::move(directory));
}

bool ReleaseHostIsProtected(const std::wstring &host) {
#ifdef NDEBUG
    PWSTR known_folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT,
                                    nullptr, &known_folder))) {
        return false;
    }
    std::wstring program_files(known_folder);
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

bool CurrentTokenElevated() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return false;
    }
    auto token =
            nonstd::make_unique_resource_checked(raw, nullptr, CloseHandle);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation,
                               sizeof(elevation), &size) != FALSE &&
           elevation.TokenIsElevated != 0;
}

std::optional<std::wstring> CurrentAdministratorSid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return std::nullopt;
    }
    auto token =
            nonstd::make_unique_resource_checked(raw, nullptr, CloseHandle);

    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user_buffer(size);
    if (size == 0 || !GetTokenInformation(token.get(), TokenUser,
                                          user_buffer.data(), size, &size)) {
        return std::nullopt;
    }
    LPWSTR sid_string = nullptr;
    if (!ConvertSidToStringSidW(static_cast<PTOKEN_USER>(
                                        static_cast<void *>(user_buffer.data()))
                                        ->User.Sid,
                                &sid_string)) {
        return std::nullopt;
    }
    std::wstring sid(sid_string);
    LocalFree(sid_string);

    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &administrators)) {
        return std::nullopt;
    }
    DWORD groups_size = 0;
    SetLastError(ERROR_SUCCESS);
    BOOL sizing_result = GetTokenInformation(token.get(), TokenGroups, nullptr,
                                             0, &groups_size);
    DWORD sizing_error = GetLastError();
    if (sizing_result == FALSE && sizing_error != ERROR_INSUFFICIENT_BUFFER) {
        DWORD error = sizing_error;
        LogError("elevate-kit: TokenGroups sizing failed, error={}", error);
        FreeSid(administrators);
        return std::nullopt;
    }
    std::vector<unsigned char> groups_buffer(groups_size);
    if (groups_size == 0 ||
        !GetTokenInformation(token.get(), TokenGroups, groups_buffer.data(),
                             groups_size, &groups_size)) {
        DWORD error = GetLastError();
        LogError("elevate-kit: TokenGroups query failed, error={}", error);
        FreeSid(administrators);
        return std::nullopt;
    }
    const auto *groups =
            reinterpret_cast<const TOKEN_GROUPS *>(groups_buffer.data());
    bool member = false;
    for (DWORD index = 0; index < groups->GroupCount; ++index) {
        if (EqualSid(groups->Groups[index].Sid, administrators) != FALSE) {
            member = true;
            break;
        }
    }
    if (!member) {
        LogDebug("administrator SID present=false");
    }
    FreeSid(administrators);
    return member ? std::optional<std::wstring>(std::move(sid)) : std::nullopt;
}

std::optional<std::pair<std::wstring, std::wstring>> AccountNamesForSid(
        const std::wstring &sid) {
    PSID sid_value = nullptr;
    if (!ConvertStringSidToSidW(sid.c_str(), &sid_value)) {
        return std::nullopt;
    }

    DWORD account_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE sid_type{};
    SetLastError(ERROR_SUCCESS);
    LookupAccountSidW(nullptr, sid_value, nullptr, &account_size, nullptr,
                      &domain_size, &sid_type);
    DWORD sizing_error = GetLastError();
    if (sizing_error != ERROR_INSUFFICIENT_BUFFER || account_size == 0) {
        LocalFree(sid_value);
        return std::nullopt;
    }

    std::vector<wchar_t> account_buffer(account_size);
    std::vector<wchar_t> domain_buffer(domain_size == 0 ? 1 : domain_size);
    if (!LookupAccountSidW(nullptr, sid_value, account_buffer.data(),
                           &account_size, domain_buffer.data(), &domain_size,
                           &sid_type)) {
        LocalFree(sid_value);
        return std::nullopt;
    }
    LocalFree(sid_value);

    std::wstring account(account_buffer.data());
    std::wstring qualified_account;
    if (domain_size != 0 && domain_buffer[0] != L'\0') {
        qualified_account.assign(domain_buffer.data());
        qualified_account.append(L"\\").append(account);
    } else {
        qualified_account = account;
    }
    return std::make_pair(std::move(account), std::move(qualified_account));
}

bool OpenService(Microsoft::WRL::ComPtr<ITaskService> &service) {
    HRESULT create_result =
            CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(service.ReleaseAndGetAddressOf()));
    if (!TaskSchedulerSucceeded(L"CoCreateInstance", create_result)) {
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    HRESULT connect_result = service->Connect(empty, empty, empty, empty);
    return TaskSchedulerSucceeded(L"ITaskService::Connect", connect_result);
}

class VariantValue {
public:
    VariantValue() {
        VariantInit(&value);
    }
    ~VariantValue() {
        VariantClear(&value);
    }
    VariantValue(const VariantValue &) = delete;
    VariantValue &operator=(const VariantValue &) = delete;
    VariantValue(VariantValue &&other) noexcept : value(other.value) {
        VariantInit(&other.value);
    }

    VARIANT value{};
};

std::optional<VariantValue> MakeRunArguments(const std::string &id) {
    SAFEARRAY *values = SafeArrayCreateVector(VT_BSTR, 0, 1);
    if (values == nullptr) {
        return std::nullopt;
    }
    std::wstring wide(id.begin(), id.end());
    BSTR value = SysAllocString(wide.c_str());
    LONG index = 0;
    if (value == nullptr) {
        SafeArrayDestroy(values);
        return std::nullopt;
    }
    HRESULT result = SafeArrayPutElement(values, &index, value);
    SysFreeString(value);
    if (FAILED(result)) {
        SafeArrayDestroy(values);
        return std::nullopt;
    }
    VariantValue arguments;
    arguments.value.vt = VT_ARRAY | VT_BSTR;
    arguments.value.parray = values;
    return arguments;
}

std::optional<std::uint64_t> RunScheduledWorker(const std::string &id) {
    ComApartment apartment;
    if (!apartment.Ready()) {
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<ITaskService> service;
    if (!OpenService(service)) {
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<ITaskFolder> folder;
    std::wstring registered_task_name = TaskName(ApplicationId::Get());
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    BSTR root = SysAllocString(L"\\");
    HRESULT folder_result =
            root == nullptr
                    ? E_OUTOFMEMORY
                    : service->GetFolder(root, folder.ReleaseAndGetAddressOf());
    SysFreeString(root);
    if (task_name == nullptr || FAILED(folder_result)) {
        SysFreeString(task_name);
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<IRegisteredTask> task;
    HRESULT task_result =
            folder->GetTask(task_name, task.ReleaseAndGetAddressOf());
    SysFreeString(task_name);
    if (FAILED(task_result)) {
        return std::nullopt;
    }
    std::optional<VariantValue> arguments = MakeRunArguments(id);
    if (!arguments) {
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<IRunningTask> running;
    HRESULT result = task->RunEx(arguments->value, 0, 0, nullptr,
                                 running.ReleaseAndGetAddressOf());
    if (!TaskSchedulerSucceeded(L"IRegisteredTask::RunEx", result)) {
        return std::nullopt;
    }
    DWORD process_id = 0;
    if (FAILED(running->get_EnginePID(&process_id)) || process_id == 0) {
        return std::nullopt;
    }
    return process_id;
}

bool InstallTask(const std::wstring &host,
                 const std::wstring &working_directory,
                 const std::wstring &user_sid,
                 const std::string &application_id) {
    LogInfo("install: connecting to Task Scheduler");
    Microsoft::WRL::ComPtr<ITaskService> service;
    if (!OpenService(service)) {
        return false;
    }
    Microsoft::WRL::ComPtr<ITaskFolder> folder;
    BSTR root = SysAllocString(L"\\");
    HRESULT folder_result =
            root == nullptr
                    ? E_OUTOFMEMORY
                    : service->GetFolder(root, folder.ReleaseAndGetAddressOf());
    SysFreeString(root);
    if (!TaskSchedulerSucceeded(L"GetFolder", folder_result)) {
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    std::wstring xml = fmt::format(Utf8ToWide(kWindowsSchedulTaskTemplate),
                                   user_sid, host, working_directory);
    std::wstring registered_task_name = TaskName(application_id);
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    BSTR xml_text = SysAllocString(xml.c_str());
    if (task_name == nullptr || xml_text == nullptr) {
        SysFreeString(xml_text);
        SysFreeString(task_name);
        return false;
    }
    Microsoft::WRL::ComPtr<IRegisteredTask> registered;
    HRESULT result =
            folder->RegisterTask(task_name, xml_text, TASK_CREATE_OR_UPDATE,
                                 empty, empty, TASK_LOGON_INTERACTIVE_TOKEN,
                                 empty, registered.ReleaseAndGetAddressOf());
    SysFreeString(task_name);
    SysFreeString(xml_text);
    if (TaskSchedulerSucceeded(L"RegisterTaskDefinition", result)) {
        LogInfo("install: Task Scheduler registration succeeded");
    }
    return SUCCEEDED(result);
}

bool TaskMatches(const std::wstring &host,
                 const std::wstring &working_directory,
                 const std::wstring &user_sid,
                 const std::string &application_id) {
    std::wstring registered_task_name = TaskName(application_id);
    auto mismatch = [&registered_task_name](const char *field) {
        LogWarn("elevate-kit: readiness task '{}' mismatch: {}",
                WideToUtf8(registered_task_name), field);
    };
    std::optional<std::pair<std::wstring, std::wstring>> names =
            AccountNamesForSid(user_sid);
    if (!names) {
        mismatch("current user account lookup");
        return false;
    }
    const std::wstring &account = names->first;
    const std::wstring &qualified_account = names->second;
    Microsoft::WRL::ComPtr<ITaskService> service;
    if (!OpenService(service)) {
        mismatch("Task Scheduler service");
        return false;
    }
    Microsoft::WRL::ComPtr<ITaskFolder> folder;
    BSTR root = SysAllocString(L"\\");
    HRESULT folder_result =
            root == nullptr
                    ? E_OUTOFMEMORY
                    : service->GetFolder(root, folder.ReleaseAndGetAddressOf());
    SysFreeString(root);
    if (FAILED(folder_result)) {
        mismatch("task folder");
        return false;
    }
    BSTR task_name = SysAllocString(registered_task_name.c_str());
    if (task_name == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<IRegisteredTask> task;
    HRESULT task_result =
            folder->GetTask(task_name, task.ReleaseAndGetAddressOf());
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
    Microsoft::WRL::ComPtr<ITaskDefinition> definition;
    Microsoft::WRL::ComPtr<IPrincipal> principal;
    Microsoft::WRL::ComPtr<ITaskSettings> settings;
    if (FAILED(task->get_Definition(definition.ReleaseAndGetAddressOf())) ||
        FAILED(definition->get_Principal(principal.ReleaseAndGetAddressOf())) ||
        FAILED(definition->get_Settings(settings.ReleaseAndGetAddressOf()))) {
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
    bool principal_matches =
            _wcsicmp(principal_user, account.c_str()) == 0 ||
            _wcsicmp(principal_user, qualified_account.c_str()) == 0;
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
    Microsoft::WRL::ComPtr<IActionCollection> actions;
    LONG count = 0;
    if (FAILED(definition->get_Actions(actions.ReleaseAndGetAddressOf())) ||
        FAILED(actions->get_Count(&count)) || count != 1) {
        mismatch("actions");
        return false;
    }
    Microsoft::WRL::ComPtr<IAction> action;
    if (FAILED(actions->get_Item(1, action.ReleaseAndGetAddressOf()))) {
        mismatch("action");
        return false;
    }
    TASK_ACTION_TYPE action_type;
    if (FAILED(action->get_Type(&action_type)) ||
        action_type != TASK_ACTION_EXEC) {
        mismatch("action type");
        return false;
    }
    Microsoft::WRL::ComPtr<IExecAction> exec;
    if (FAILED(action->QueryInterface(
                IID_PPV_ARGS(exec.ReleaseAndGetAddressOf())))) {
        mismatch("exec action");
        return false;
    }
    BSTR path = nullptr;
    BSTR directory = nullptr;
    BSTR arguments = nullptr;
    HRESULT path_result = exec->get_Path(&path);
    HRESULT directory_result = exec->get_WorkingDirectory(&directory);
    HRESULT arguments_result = exec->get_Arguments(&arguments);
    bool matches = SUCCEEDED(path_result) && SUCCEEDED(directory_result) &&
                   SUCCEEDED(arguments_result) && path != nullptr &&
                   directory != nullptr && arguments != nullptr &&
                   _wcsicmp(path, host.c_str()) == 0 &&
                   _wcsicmp(directory, working_directory.c_str()) == 0 &&
                   wcscmp(arguments, L"--elevate-pipe=$(Arg0)") == 0;
    SysFreeString(path);
    SysFreeString(directory);
    SysFreeString(arguments);
    if (!matches) {
        mismatch("action path, working directory, or arguments");
    }
    return matches;
}

std::wstring WorkerCommandLine(const std::vector<std::string> &arguments) {
    std::wstring command_line;
    for (const std::string &argument : arguments) {
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line.push_back(L'"');
        std::wstring wide(argument.begin(), argument.end());
        std::size_t backslashes = 0;
        for (wchar_t character : wide) {
            if (character == L'\\') {
                ++backslashes;
            } else if (character == L'"') {
                command_line.append(backslashes * 2 + 1, L'\\');
                command_line.push_back(L'"');
                backslashes = 0;
            } else {
                command_line.append(backslashes, L'\\');
                backslashes = 0;
                command_line.push_back(character);
            }
        }
        command_line.append(backslashes * 2, L'\\');
        command_line.push_back(L'"');
    }
    return command_line;
}

class WindowsSilentElevation final : public ISilentElevation {
public:
    bool IsElevated() override {
        return CurrentTokenElevated() && CurrentAdministratorSid().has_value();
    }
    bool IsSilentElevationConfigured(
            const std::string &application_id) override {
        LogDebug("readiness: checking");
        std::optional<std::wstring> user_sid = CurrentAdministratorSid();
        if (!user_sid) {
            LogWarn("readiness: administrator check failed");
            return false;
        }
        std::optional<std::pair<std::wstring, std::wstring>> current =
                CurrentExecutablePathAndDirectory();
        if (!current || !ReleaseHostIsProtected(current->first)) {
            LogWarn("readiness: host path check failed");
            return false;
        }
        ComApartment apartment;
        if (!apartment.Ready()) {
            LogWarn("readiness: COM apartment unavailable");
            return false;
        }
        bool ready = TaskMatches(current->first, current->second, *user_sid,
                                 application_id);
        if (ready) {
            LogInfo("readiness: ready");
        } else {
            LogWarn("readiness: task mismatch or missing");
        }
        return ready;
    }
    bool ConfigureSilentElevation(const std::string &application_id) override {
        LogInfo("install: checking elevated administrator token");
        std::optional<std::wstring> user_sid = CurrentAdministratorSid();
        if (!CurrentTokenElevated() || !user_sid) {
            LogWarn("install: elevated administrator check failed");
            return false;
        }
        std::optional<std::pair<std::wstring, std::wstring>> current =
                CurrentExecutablePathAndDirectory();
        if (!current || !ReleaseHostIsProtected(current->first)) {
            LogWarn("install: host path check failed");
            return false;
        }
        ComApartment apartment;
        if (!apartment.Ready()) {
            LogWarn("install: COM apartment unavailable");
            return false;
        }
        return InstallTask(current->first, current->second, *user_sid,
                           application_id);
    }
    std::optional<std::uint64_t> LaunchElevatedService(
            const std::vector<std::string> &arguments) override {
        bool use_task_scheduler = true;
        for (const std::string &argument : arguments) {
            if (argument == "--elevate-install") {
                use_task_scheduler = false;
                break;
            }
        }
        if (use_task_scheduler) {
            constexpr char kPipeArgument[] = "--elevate-pipe=";
            std::optional<std::string> pipe_id;
            for (const std::string &argument : arguments) {
                if (argument.rfind(kPipeArgument, 0) == 0) {
                    pipe_id = argument.substr(sizeof(kPipeArgument) - 1);
                    break;
                }
            }
            if (!pipe_id || pipe_id->empty()) {
                return std::nullopt;
            }
            return RunScheduledWorker(*pipe_id);
        }
        {
            std::optional<std::pair<std::wstring, std::wstring>> current =
                    CurrentExecutablePathAndDirectory();
            if (!current) {
                return std::nullopt;
            }
            std::wstring worker_arguments = WorkerCommandLine(arguments);

            SHELLEXECUTEINFOW execute{};
            execute.cbSize = sizeof(execute);
            execute.fMask = SEE_MASK_NOCLOSEPROCESS;
            execute.lpVerb = L"runas";
            execute.lpFile = current->first.c_str();
            execute.lpParameters = worker_arguments.c_str();
            execute.lpDirectory = current->second.c_str();
            LogInfo("executeRequest: requesting UAC worker");
            if (!ShellExecuteExW(&execute)) {
                LogError("elevate-kit: ShellExecuteEx failed, error={}",
                         GetLastError());
                return std::nullopt;
            }
            auto launched_process = nonstd::make_unique_resource_checked(
                    execute.hProcess, nullptr, CloseHandle);
            DWORD process_id = GetProcessId(execute.hProcess);
            if (process_id == 0) {
                return std::nullopt;
            }
            return process_id;
        }
    }
};

}  // namespace

std::unique_ptr<ISilentElevation> ISilentElevation::Create() {
    return std::make_unique<WindowsSilentElevation>();
}

}  // namespace elevate_kit
