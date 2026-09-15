#include "windows_environment_probe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <powersetting.h>
#include <powrprof.h>
#include <winsvc.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace px::panel::product {
namespace {

using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, decltype(&CloseServiceHandle)>;
using ModuleHandle = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;
using RegistryHandle = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)>;

void FreePowerScheme(GUID* value) noexcept { // NOLINT(pixels-raw-pointer-boundary) LocalFree owns the PowerGetActiveScheme result.
    if (value)
        static_cast<void>(LocalFree(value));
}

using PowerSchemeHandle = std::unique_ptr<GUID, decltype(&FreePowerScheme)>;

enum class ServiceState : std::uint8_t { Running, Stopped, Pending, Unavailable };

struct PowerTimeouts final {
    std::optional<std::uint32_t> ac{};
    std::optional<std::uint32_t> dc{};
};

class ComApartment final {
  public:
    ComApartment() noexcept {
        const HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
        available_ = SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
        uninitialize_ = result == S_OK || result == S_FALSE;
    }
    ~ComApartment() {
        if (uninitialize_)
            CoUninitialize();
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    [[nodiscard]] bool Available() const noexcept {
        return available_;
    }

  private:
    bool available_{};
    bool uninitialize_{};
};

std::string HexResult(const HRESULT result) {
    return std::format("HRESULT 0x{:08X}", static_cast<std::uint32_t>(result));
}

ServiceState QueryService(const std::wstring_view name) {
    ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT), &CloseServiceHandle};
    if (!manager)
        return ServiceState::Unavailable;
    const std::wstring ownedName{name};
    ServiceHandle service{OpenServiceW(manager.get(), ownedName.c_str(), SERVICE_QUERY_STATUS), &CloseServiceHandle};
    if (!service)
        return ServiceState::Unavailable;

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded{};
    if (!QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(std::addressof(status)), sizeof(status),
                              std::addressof(bytesNeeded))) { // NOLINT(pixels-raw-pointer-boundary) Win32 fills caller-owned storage.
        return ServiceState::Unavailable;
    }
    if (status.dwCurrentState == SERVICE_RUNNING)
        return ServiceState::Running;
    if (status.dwCurrentState == SERVICE_START_PENDING || status.dwCurrentState == SERVICE_STOP_PENDING ||
        status.dwCurrentState == SERVICE_CONTINUE_PENDING || status.dwCurrentState == SERVICE_PAUSE_PENDING) {
        return ServiceState::Pending;
    }
    return ServiceState::Stopped;
}

bool ModuleExistsInSystemDirectory(const std::wstring_view name) {
    const std::wstring ownedName{name};
    ModuleHandle module{LoadLibraryExW(ownedName.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), &FreeLibrary};
    return module != nullptr;
}

std::optional<std::wstring> ReadWinlogonString(const std::wstring_view name) {
    constexpr std::wstring_view key{L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"};
    const std::wstring ownedName{name};
    std::array<wchar_t, 512> value{};
    DWORD bytes{static_cast<DWORD>(value.size() * sizeof(wchar_t))};
    const LSTATUS result{RegGetValueW(HKEY_LOCAL_MACHINE, key.data(), ownedName.c_str(), RRF_RT_REG_SZ | RRF_ZEROONFAILURE, nullptr, value.data(),
                                      std::addressof(bytes))};
    if (result != ERROR_SUCCESS)
        return std::nullopt;
    return std::wstring{value.data()};
}

bool RegistryKeyExists(const std::wstring_view path) {
    HKEY openedKey{}; // NOLINT(pixels-raw-pointer-boundary) RegOpenKeyExW returns a transient handle wrapped immediately below.
    const std::wstring ownedPath{path};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ownedPath.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                      std::addressof(openedKey)) != ERROR_SUCCESS) { // NOLINT(pixels-raw-pointer-boundary) Win32 output boundary.
        return false;
    }
    RegistryHandle key{openedKey, &RegCloseKey};
    return key != nullptr;
}

bool HasPendingFileRenameOperations() {
    constexpr std::wstring_view key{L"SYSTEM\\CurrentControlSet\\Control\\Session Manager"};
    DWORD bytes{};
    const LSTATUS result{
        RegGetValueW(HKEY_LOCAL_MACHINE, key.data(), L"PendingFileRenameOperations", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, std::addressof(bytes))};
    return result == ERROR_SUCCESS && bytes > sizeof(wchar_t) * 2U;
}

PowerSchemeHandle ActivePowerScheme() {
    GUID* activeScheme{}; // NOLINT(pixels-raw-pointer-boundary) PowerGetActiveScheme allocates this result for immediate RAII adoption.
    if (PowerGetActiveScheme(nullptr, std::addressof(activeScheme)) != ERROR_SUCCESS)
        return {nullptr, &FreePowerScheme};
    return {activeScheme, &FreePowerScheme};
}

std::optional<std::uint32_t> ReadPowerTimeout(const GUID& scheme, const GUID& subgroup, const GUID& setting, const bool acPower) {
    DWORD seconds{};
    const DWORD result{
        acPower ? PowerReadACValueIndex(nullptr, std::addressof(scheme), std::addressof(subgroup), std::addressof(setting), std::addressof(seconds))
                : PowerReadDCValueIndex(nullptr, std::addressof(scheme), std::addressof(subgroup), std::addressof(setting), std::addressof(seconds))};
    if (result != ERROR_SUCCESS)
        return std::nullopt;
    return seconds;
}

PowerTimeouts ProbePowerTimeouts(const GUID& scheme, const GUID& subgroup, const GUID& setting) {
    SYSTEM_POWER_STATUS powerStatus{};
    const bool hasBattery{GetSystemPowerStatus(std::addressof(powerStatus)) && powerStatus.BatteryFlag != 128U && powerStatus.BatteryFlag != 255U};
    return {.ac = ReadPowerTimeout(scheme, subgroup, setting, true),
            .dc = hasBattery ? ReadPowerTimeout(scheme, subgroup, setting, false) : std::nullopt};
}

ui::EnvironmentCheckStatus ProbeAudio() {
    ui::EnvironmentCheckStatus result{.id = ui::EnvironmentCheckId::Audio, .action = ui::EnvironmentAction::OpenSoundSettings};
    const ServiceState audioService{QueryService(L"Audiosrv")};
    const ServiceState endpointBuilder{QueryService(L"AudioEndpointBuilder")};
    if (audioService != ServiceState::Running || endpointBuilder != ServiceState::Running) {
        result.state = audioService == ServiceState::Unavailable || endpointBuilder == ServiceState::Unavailable
                           ? ui::EnvironmentCheckState::Unavailable
                           : ui::EnvironmentCheckState::Warning;
        result.technicalDetail = audioService == ServiceState::Pending || endpointBuilder == ServiceState::Pending
                                     ? "Audiosrv / AudioEndpointBuilder pending"
                                     : "Audiosrv / AudioEndpointBuilder";
        return result;
    }

    const ComApartment apartment{};
    if (!apartment.Available()) {
        result.state = ui::EnvironmentCheckState::Unavailable;
        result.technicalDetail = "COM";
        return result;
    }
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
    HRESULT status{CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(enumerator.GetAddressOf()))};
    if (FAILED(status)) {
        result.state = ui::EnvironmentCheckState::Unavailable;
        result.technicalDetail = HexResult(status);
        return result;
    }
    Microsoft::WRL::ComPtr<IMMDevice> endpoint{};
    status = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, endpoint.GetAddressOf());
    if (FAILED(status)) {
        result.state = ui::EnvironmentCheckState::Warning;
        result.technicalDetail = HexResult(status);
        return result;
    }
    Microsoft::WRL::ComPtr<IAudioClient> audioClient{};
    status = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(audioClient.GetAddressOf())); // NOLINT(pixels-raw-pointer-boundary) COM output boundary.
    result.state = SUCCEEDED(status) ? ui::EnvironmentCheckState::Ready : ui::EnvironmentCheckState::Warning;
    result.technicalDetail = SUCCEEDED(status) ? "WASAPI" : HexResult(status);
    return result;
}

ui::EnvironmentCheckStatus ProbeVisualCppRuntime() {
    struct RequiredModule final {
        std::wstring_view systemName{};
        std::string_view displayName{};
    };
    constexpr std::array required{RequiredModule{L"msvcp140.dll", "msvcp140.dll"}, RequiredModule{L"vcruntime140.dll", "vcruntime140.dll"},
                                  RequiredModule{L"vcruntime140_1.dll", "vcruntime140_1.dll"}};
    std::vector<std::string> missing{};
    for (const auto& module : required) {
        if (!ModuleExistsInSystemDirectory(module.systemName))
            missing.emplace_back(module.displayName);
    }
    ui::EnvironmentCheckStatus result{.id = ui::EnvironmentCheckId::VisualCppRuntime,
                                      .state = missing.empty() ? ui::EnvironmentCheckState::Ready : ui::EnvironmentCheckState::Warning,
                                      .action = missing.empty() ? ui::EnvironmentAction::None : ui::EnvironmentAction::DownloadVisualCppRuntime};
    if (!missing.empty()) {
        for (const auto& name : missing) {
            if (!result.technicalDetail.empty())
                result.technicalDetail += ", ";
            result.technicalDetail += name;
        }
    } else {
        result.technicalDetail = "MSVC v14 x64";
    }
    return result;
}

ui::EnvironmentCheckStatus ProbeLegacyDirectXRuntime() {
    const bool available{ModuleExistsInSystemDirectory(L"xinput1_3.dll")};
    return {.id = ui::EnvironmentCheckId::LegacyDirectXRuntime,
            .state = available ? ui::EnvironmentCheckState::Ready : ui::EnvironmentCheckState::Recommendation,
            .action = available ? ui::EnvironmentAction::None : ui::EnvironmentAction::DownloadLegacyDirectXRuntime,
            .technicalDetail = "XInput 1.3 (xinput1_3.dll)"};
}

ui::EnvironmentCheckStatus ProbeAutoLogin() {
    ui::EnvironmentCheckStatus result{.id = ui::EnvironmentCheckId::WindowsAutoLogin,
                                      .state = ui::EnvironmentCheckState::NotConfigured,
                                      .action = ui::EnvironmentAction::OpenAutoLoginHelp};
    const auto enabled = ReadWinlogonString(L"AutoAdminLogon");
    if (!enabled || *enabled != L"1") {
        result.technicalDetail = "AutoAdminLogon=0";
        return result;
    }
    const bool hasUser{ReadWinlogonString(L"DefaultUserName").value_or(L"").empty() == false};
    const bool hasLegalNotice{!ReadWinlogonString(L"LegalNoticeCaption").value_or(L"").empty() ||
                              !ReadWinlogonString(L"LegalNoticeText").value_or(L"").empty()};
    result.state = hasUser && !hasLegalNotice ? ui::EnvironmentCheckState::Ready : ui::EnvironmentCheckState::Warning;
    result.technicalDetail = hasLegalNotice ? "AutoAdminLogon=1, legal notice policy" : "AutoAdminLogon=1";
    return result;
}

ui::EnvironmentCheckStatus MakePowerTimeoutStatus(const ui::EnvironmentCheckId id, const PowerTimeouts& timeouts) {
    const bool readable{timeouts.ac.has_value() || timeouts.dc.has_value()};
    const bool never{(!timeouts.ac || *timeouts.ac == 0U) && (!timeouts.dc || *timeouts.dc == 0U)};
    return {.id = id,
            .state = !readable ? ui::EnvironmentCheckState::Unavailable
                     : never   ? ui::EnvironmentCheckState::Ready
                               : ui::EnvironmentCheckState::Recommendation,
            .action = ui::EnvironmentAction::OpenPowerSettings,
            .acTimeoutSeconds = timeouts.ac,
            .dcTimeoutSeconds = timeouts.dc};
}

ui::EnvironmentCheckStatus ProbeHighPerformanceMode(const GUID& scheme) {
    constexpr GUID ultimatePerformance{0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};
    const bool highPerformance{IsEqualGUID(scheme, GUID_MIN_POWER_SAVINGS) || IsEqualGUID(scheme, ultimatePerformance)};
    return {.id = ui::EnvironmentCheckId::HighPerformanceMode,
            .state = highPerformance ? ui::EnvironmentCheckState::Ready : ui::EnvironmentCheckState::Recommendation,
            .action = ui::EnvironmentAction::OpenPowerSettings};
}

ui::EnvironmentCheckStatus ProbePendingRestart() {
    const bool componentServicing{RegistryKeyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending")};
    const bool windowsUpdate{RegistryKeyExists(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired")};
    const bool fileRename{HasPendingFileRenameOperations()};
    ui::EnvironmentCheckStatus result{
        .id = ui::EnvironmentCheckId::PendingRestart,
        .state = componentServicing || windowsUpdate || fileRename ? ui::EnvironmentCheckState::Warning : ui::EnvironmentCheckState::Ready,
        .action = componentServicing || windowsUpdate || fileRename ? ui::EnvironmentAction::OpenWindowsUpdateSettings : ui::EnvironmentAction::None};
    if (componentServicing)
        result.technicalDetail = "CBS";
    if (windowsUpdate)
        result.technicalDetail += result.technicalDetail.empty() ? "Windows Update" : ", Windows Update";
    if (fileRename)
        result.technicalDetail += result.technicalDetail.empty() ? "PendingFileRenameOperations" : ", PendingFileRenameOperations";
    return result;
}

} // namespace

std::vector<ui::EnvironmentCheckStatus> ProbeWindowsEnvironment() {
    std::vector<ui::EnvironmentCheckStatus> checks{};
    checks.reserve(8);
    checks.push_back(ProbeAudio());
    checks.push_back(ProbeVisualCppRuntime());
    checks.push_back(ProbeLegacyDirectXRuntime());
    checks.push_back(ProbeAutoLogin());

    const auto scheme = ActivePowerScheme();
    if (scheme) {
        checks.push_back(MakePowerTimeoutStatus(ui::EnvironmentCheckId::DisplayTimeout,
                                                ProbePowerTimeouts(*scheme, GUID_VIDEO_SUBGROUP, GUID_VIDEO_POWERDOWN_TIMEOUT)));
        checks.push_back(
            MakePowerTimeoutStatus(ui::EnvironmentCheckId::SleepTimeout, ProbePowerTimeouts(*scheme, GUID_SLEEP_SUBGROUP, GUID_STANDBY_TIMEOUT)));
        checks.push_back(ProbeHighPerformanceMode(*scheme));
    } else {
        checks.push_back({.id = ui::EnvironmentCheckId::DisplayTimeout,
                          .state = ui::EnvironmentCheckState::Unavailable,
                          .action = ui::EnvironmentAction::OpenPowerSettings});
        checks.push_back({.id = ui::EnvironmentCheckId::SleepTimeout,
                          .state = ui::EnvironmentCheckState::Unavailable,
                          .action = ui::EnvironmentAction::OpenPowerSettings});
        checks.push_back({.id = ui::EnvironmentCheckId::HighPerformanceMode,
                          .state = ui::EnvironmentCheckState::Unavailable,
                          .action = ui::EnvironmentAction::OpenPowerSettings});
    }
    checks.push_back(ProbePendingRestart());
    return checks;
}

} // namespace px::panel::product
