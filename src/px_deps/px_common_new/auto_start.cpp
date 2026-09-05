#ifdef WIN32

#include "auto_start.h"

#include <algorithm>
#include <comdef.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "px_common_new/log.h"

namespace px {
namespace {

struct RegistryKeyCloser final {
    void operator()(HKEY key) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HKEY is an opaque Win32 handle.
        RegCloseKey(key);
    }
};

using UniqueRegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, RegistryKeyCloser>;

std::optional<std::wstring> Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return std::wstring{};
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (required <= 0) return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), wide.data(), required) != required) {
        return std::nullopt;
    }
    return wide;
}

bool SetAutoStartInternal(HKEY root_key, std::wstring_view sub_key, const std::filesystem::path& executable, bool enabled) {
    if (executable.empty()) {
        LOGE("event=autostart.registry code=INVALID_PATH operation=update outcome=failed recoverable=false");
        return false;
    }

    HKEY opened_key{};  // NOLINT(gammaray-raw-pointer-boundary): RegCreateKeyExW transfers the key through HKEY*.
    const auto sub_key_text = std::wstring{sub_key};
    const auto open_result = RegCreateKeyExW(root_key, sub_key_text.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                                             &opened_key, nullptr);
    if (open_result != ERROR_SUCCESS) {
        LOGE("event=autostart.registry code=OPEN_FAILED operation=open outcome=failed recoverable=true win32_error={}", open_result);
        return false;
    }
    const UniqueRegistryKey key{opened_key};

    const auto name = executable.stem().native();
    auto command = executable.native();
    std::ranges::replace(command, L'/', L'\\');

    if (!enabled) {
        const auto result = RegDeleteValueW(key.get(), name.c_str());
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            LOGE("event=autostart.registry code=DELETE_FAILED operation=disable outcome=failed recoverable=true win32_error={}", result);
            return false;
        }
        return true;
    }

    std::vector<wchar_t> existing(32'768, L'\0');
    DWORD existing_size = static_cast<DWORD>(existing.size() * sizeof(wchar_t));
    DWORD type{};
    const auto query_result = RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(existing.data()), &existing_size);
    if (query_result == ERROR_SUCCESS && type == REG_SZ && std::wstring_view{existing.data()} == command) return true;

    const auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const auto result = RegSetValueExW(key.get(), name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    if (result != ERROR_SUCCESS) {
        LOGE("event=autostart.registry code=WRITE_FAILED operation=enable outcome=failed recoverable=true win32_error={}", result);
        return false;
    }
    return true;
}

bool CheckHresult(HRESULT result, std::string_view operation) {
    if (SUCCEEDED(result)) return true;
    LOGE("event=autostart.task code=COM_FAILED operation={} outcome=failed recoverable=true hresult=0x{:08x}", operation,
         static_cast<unsigned>(result));
    return false;
}

}  // namespace

bool AutoStart::SetAutoStart(const std::filesystem::path& executable, bool enabled) {
    return SetAutoStartInternal(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", executable, enabled);
}

bool AutoStart::SetAutoStartAdmin(const std::filesystem::path& executable, bool enabled) {
    return SetAutoStartInternal(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", executable, enabled);
}

AutoStart::AutoStart() {
    const auto initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (initialize_result == S_OK || initialize_result == S_FALSE) {
        com_initialized_ = true;
    } else if (!CheckHresult(initialize_result, "initialize_com")) {
        return;
    }

    if (!CheckHresult(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(task_service_.GetAddressOf())),
                      "create_task_service")) {
        return;
    }
    if (!CheckHresult(task_service_->Connect(_variant_t{}, _variant_t{}, _variant_t{}, _variant_t{}), "connect_task_service")) return;
    static_cast<void>(CheckHresult(task_service_->GetFolder(_bstr_t{L"\\"}, root_folder_.GetAddressOf()), "get_root_folder"));
}

AutoStart::~AutoStart() {
    root_folder_.Reset();
    task_service_.Reset();
    if (com_initialized_) CoUninitialize();
}

bool AutoStart::IsReady() const noexcept {
    return task_service_ && root_folder_;
}

bool AutoStart::CreateLogonTask(std::string_view task_name, const std::filesystem::path& executable, std::string_view arguments,
                                std::string_view author) {
    const auto wide_task_name = Utf8ToWide(task_name);
    const auto wide_arguments = Utf8ToWide(arguments);
    const auto wide_author = Utf8ToWide(author);
    if (!IsReady() || !wide_task_name || !wide_arguments || !wide_author || task_name.empty() || executable.empty()) {
        LOGE("event=autostart.task code=INVALID_ARGUMENT operation=create outcome=failed recoverable=false");
        return false;
    }

    static_cast<void>(Delete(task_name));
    Microsoft::WRL::ComPtr<ITaskDefinition> task{};
    if (!CheckHresult(task_service_->NewTask(0, task.GetAddressOf()), "new_task")) return false;

    Microsoft::WRL::ComPtr<IRegistrationInfo> registration{};
    if (!CheckHresult(task->get_RegistrationInfo(registration.GetAddressOf()), "get_registration_info") ||
        !CheckHresult(registration->put_Author(_bstr_t{wide_author->c_str()}), "set_author")) {
        return false;
    }

    Microsoft::WRL::ComPtr<IPrincipal> principal{};
    if (!CheckHresult(task->get_Principal(principal.GetAddressOf()), "get_principal") ||
        !CheckHresult(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN), "set_logon_type") ||
        !CheckHresult(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST), "set_run_level")) {
        return false;
    }

    Microsoft::WRL::ComPtr<ITaskSettings> settings{};
    if (!CheckHresult(task->get_Settings(settings.GetAddressOf()), "get_settings") ||
        !CheckHresult(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE), "set_battery_stop") ||
        !CheckHresult(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE), "set_battery_start") ||
        !CheckHresult(settings->put_AllowDemandStart(VARIANT_TRUE), "set_demand_start") ||
        !CheckHresult(settings->put_StartWhenAvailable(VARIANT_FALSE), "set_start_when_available") ||
        !CheckHresult(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW), "set_instance_policy")) {
        return false;
    }

    Microsoft::WRL::ComPtr<IActionCollection> actions{};
    Microsoft::WRL::ComPtr<IAction> action{};
    Microsoft::WRL::ComPtr<IExecAction> executable_action{};
    if (!CheckHresult(task->get_Actions(actions.GetAddressOf()), "get_actions") ||
        !CheckHresult(actions->Create(TASK_ACTION_EXEC, action.GetAddressOf()), "create_exec_action") ||
        !CheckHresult(action.As(&executable_action), "query_exec_action") ||
        !CheckHresult(executable_action->put_Path(_bstr_t{executable.native().c_str()}), "set_executable") ||
        !CheckHresult(executable_action->put_Arguments(_bstr_t{wide_arguments->c_str()}), "set_arguments")) {
        return false;
    }

    Microsoft::WRL::ComPtr<ITriggerCollection> triggers{};
    Microsoft::WRL::ComPtr<ITrigger> trigger{};
    if (!CheckHresult(task->get_Triggers(triggers.GetAddressOf()), "get_triggers") ||
        !CheckHresult(triggers->Create(TASK_TRIGGER_LOGON, trigger.GetAddressOf()), "create_logon_trigger")) {
        return false;
    }

    Microsoft::WRL::ComPtr<IRegisteredTask> registered{};
    const auto result = root_folder_->RegisterTaskDefinition(_bstr_t{wide_task_name->c_str()}, task.Get(), TASK_CREATE_OR_UPDATE, _variant_t{},
                                                             _variant_t{}, TASK_LOGON_INTERACTIVE_TOKEN, _variant_t{L""},
                                                             registered.GetAddressOf());
    return CheckHresult(result, "register_task");
}

bool AutoStart::Delete(std::string_view task_name) {
    const auto wide_task_name = Utf8ToWide(task_name);
    return root_folder_ && wide_task_name && SUCCEEDED(root_folder_->DeleteTask(_bstr_t{wide_task_name->c_str()}, 0));
}

}  // namespace px

#endif  // WIN32
