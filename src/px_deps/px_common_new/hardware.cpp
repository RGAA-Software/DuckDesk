#include "hardware.h"

#include <winsock2.h>
#include <Windows.h>
#include <comdef.h>
#include <iphlpapi.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <ws2tcpip.h>

#include <array>
#include <charconv>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

#include "px_common_new/log.h"
#include "px_common_new/num_formatter.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/string_util.h"

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "IPHLPAPI.lib")

namespace px {
namespace {

constexpr std::string_view kKeyCpuName{"key_cpu_name"};
constexpr std::string_view kKeyCpuCores{"key_cpu_cores"};
constexpr std::string_view kKeyCpuId{"key_cpu_id"};
constexpr std::string_view kKeyCpuMaxClock{"key_cpu_max_clock"};

class ComApartment final {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), owns_(result_ == S_OK || result_ == S_FALSE) {}
    ~ComApartment() {
        if (owns_) CoUninitialize();
    }
    [[nodiscard]] bool IsUsable() const noexcept { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
    [[nodiscard]] HRESULT Result() const noexcept { return result_; }

private:
    HRESULT result_{E_FAIL};
    bool owns_{false};
};

struct WinHandleCloser final {
    void operator()(void* handle) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HANDLE is an opaque Win32 handle.
        if (handle) CloseHandle(handle);
    }
};

using UniqueWinHandle = std::unique_ptr<void, WinHandleCloser>;
using Microsoft::WRL::ComPtr;

std::optional<std::string> ReadStringProperty(IWbemClassObject& object, std::wstring_view name) {
    _variant_t value{};
    const auto property_name = std::wstring{name};
    if (FAILED(object.Get(property_name.c_str(), 0, std::addressof(value), nullptr, nullptr)) || value.vt != VT_BSTR || !value.bstrVal) {
        return std::nullopt;
    }
    return StringUtil::ToUTF8(value.bstrVal);
}

std::optional<std::uint64_t> ReadUnsignedProperty(IWbemClassObject& object, std::wstring_view name) {
    _variant_t value{};
    const auto property_name = std::wstring{name};
    if (FAILED(object.Get(property_name.c_str(), 0, std::addressof(value), nullptr, nullptr))) return std::nullopt;
    switch (value.vt) {
    case VT_UI1:
        return value.bVal;
    case VT_UI2:
        return value.uiVal;
    case VT_UI4:
        return value.ulVal;
    case VT_UI8:
        return value.ullVal;
    case VT_I2:
        return value.iVal >= 0 ? std::optional<std::uint64_t>{static_cast<std::uint64_t>(value.iVal)} : std::nullopt;
    case VT_I4:
        return value.lVal >= 0 ? std::optional<std::uint64_t>{static_cast<std::uint64_t>(value.lVal)} : std::nullopt;
    default:
        return std::nullopt;
    }
}

template <typename Visitor>
bool QueryWmi(IWbemServices& service, std::wstring_view query, Visitor&& visitor) {
    ComPtr<IEnumWbemClassObject> enumerator{};
    const auto query_text = std::wstring{query};
    const auto result = service.ExecQuery(_bstr_t{L"WQL"}, _bstr_t{query_text.c_str()}, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                          nullptr, enumerator.GetAddressOf());
    if (FAILED(result) || !enumerator) {
        LOGE("event=hardware.wmi code=QUERY_FAILED operation=query outcome=failed recoverable=true hresult=0x{:08x}",
             static_cast<unsigned>(result));
        return false;
    }

    for (;;) {
        ComPtr<IWbemClassObject> object{};
        ULONG returned{0};
        const auto next_result = enumerator->Next(WBEM_INFINITE, 1, object.GetAddressOf(), &returned);
        if (FAILED(next_result)) {
            LOGE("event=hardware.wmi code=ENUMERATE_FAILED operation=next outcome=failed recoverable=true hresult=0x{:08x}",
                 static_cast<unsigned>(next_result));
            return false;
        }
        if (returned == 0 || !object) return true;
        std::invoke(visitor, *object.Get());
    }
}

std::uint32_t ParseUnsignedOrZero(std::string_view value) noexcept {
    std::uint32_t output{0};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} ? output : 0;
}

}  // namespace

Hardware& Hardware::Instance() {
    static Hardware hardware{};
    return hardware;
}

int Hardware::Detect(bool read_cpu_info, bool detect_disks, bool detect_drivers) {
    const auto preferences = SharedPreference::Instance();
    if (preferences->Get(std::string{kKeyCpuName}).empty()) read_cpu_info = true;

    desktop_name_ = GetDesktopName();
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memory_status)) memory_size_ = static_cast<std::size_t>(memory_status.ullTotalPhys);

    const ComApartment apartment{};
    if (!apartment.IsUsable()) {
        LOGE("event=hardware.wmi code=COM_INIT_FAILED operation=detect outcome=failed recoverable=true hresult=0x{:08x}",
             static_cast<unsigned>(apartment.Result()));
        return -1;
    }
    const auto security_result = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                                                      nullptr, EOAC_NONE, nullptr);
    if (FAILED(security_result) && security_result != RPC_E_TOO_LATE) {
        LOGE("event=hardware.wmi code=COM_SECURITY_FAILED operation=detect outcome=failed recoverable=true hresult=0x{:08x}",
             static_cast<unsigned>(security_result));
        return -1;
    }

    ComPtr<IWbemLocator> locator{};
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(locator.GetAddressOf()))) || !locator) return -1;
    ComPtr<IWbemServices> service{};
    if (FAILED(locator->ConnectServer(_bstr_t{L"ROOT\\CIMV2"}, nullptr, nullptr, nullptr, 0, nullptr, nullptr, service.GetAddressOf())) || !service) {
        return -1;
    }
    if (FAILED(CoSetProxyBlanket(service.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) {
        return -1;
    }

    hw_disks_.clear();
    drivers_.clear();
    gpus_.clear();

    if (read_cpu_info) {
        if (!QueryWmi(*service.Get(), L"SELECT * FROM Win32_Processor", [&](IWbemClassObject& object) {
                if (const auto value = ReadStringProperty(object, L"Name")) hw_cpu_.name_ = *value;
                if (const auto value = ReadStringProperty(object, L"ProcessorId")) hw_cpu_.id_ = *value;
                if (const auto value = ReadUnsignedProperty(object, L"NumberOfCores")) hw_cpu_.num_cores_ = static_cast<std::uint32_t>(*value);
                if (const auto value = ReadUnsignedProperty(object, L"MaxClockSpeed")) {
                    hw_cpu_.max_clock_speed_ = static_cast<std::uint32_t>(*value);
                }
            })) {
            return -1;
        }
        preferences->Put(std::string{kKeyCpuName}, hw_cpu_.name_);
        preferences->Put(std::string{kKeyCpuId}, hw_cpu_.id_);
        preferences->Put(std::string{kKeyCpuCores}, std::to_string(hw_cpu_.num_cores_));
        preferences->Put(std::string{kKeyCpuMaxClock}, std::to_string(hw_cpu_.max_clock_speed_));
    } else {
        hw_cpu_.name_ = preferences->Get(std::string{kKeyCpuName});
        hw_cpu_.id_ = preferences->Get(std::string{kKeyCpuId});
        hw_cpu_.num_cores_ = ParseUnsignedOrZero(preferences->Get(std::string{kKeyCpuCores}));
        hw_cpu_.max_clock_speed_ = ParseUnsignedOrZero(preferences->Get(std::string{kKeyCpuMaxClock}));
    }

    if (detect_disks && !QueryWmi(*service.Get(), L"SELECT * FROM Win32_DiskDrive", [&](IWbemClassObject& object) {
            HwDisk disk{};
            disk.name_ = ReadStringProperty(object, L"Name").value_or("");
            disk.model_ = ReadStringProperty(object, L"Model").value_or("");
            disk.status_ = ReadStringProperty(object, L"Status").value_or("");
            disk.serial_number_ = ReadStringProperty(object, L"SerialNumber").value_or("");
            disk.interface_type_ = ReadStringProperty(object, L"InterfaceType").value_or("");
            if (disk.interface_type_ == "IDE") hw_disks_.push_back(std::move(disk));
        })) {
        return -1;
    }

    if (detect_drivers && !QueryWmi(*service.Get(), L"SELECT * FROM Win32_SystemDriver", [&](IWbemClassObject& object) {
            drivers_.push_back(SysDriver{.name_ = ReadStringProperty(object, L"Name").value_or(""),
                                         .display_name_ = ReadStringProperty(object, L"DisplayName").value_or(""),
                                         .state_ = ReadStringProperty(object, L"State").value_or("")});
        })) {
        return -1;
    }

    if (!QueryWmi(*service.Get(), L"SELECT * FROM Win32_VideoController", [&](IWbemClassObject& object) {
            HwGPU gpu{.name_ = ReadStringProperty(object, L"Name").value_or(""),
                      .size_ = ReadUnsignedProperty(object, L"AdapterRAM").value_or(0),
                      .res_w_ = static_cast<int>(ReadUnsignedProperty(object, L"CurrentHorizontalResolution").value_or(0)),
                      .res_h_ = static_cast<int>(ReadUnsignedProperty(object, L"CurrentVerticalResolution").value_or(0)),
                      .driver_version_ = ReadStringProperty(object, L"DriverVersion").value_or(""),
                      .pnp_device_id_ = ReadStringProperty(object, L"PNPDeviceID").value_or("")};
            gpu.size_str_ = NumFormatter::FormatStorageSize(gpu.size_);
            if (gpu.name_.find("Virtual") == std::string::npos && gpu.size_ >= 128ULL * 1024ULL * 1024ULL) gpus_.push_back(std::move(gpu));
        })) {
        return -1;
    }

    DetectMac();
    return 0;
}

void Hardware::Dump() const {
    LOGI("hardware desktop={} cpu_id={} cpu_name={} cpu_cores={} memory={}", desktop_name_, hw_cpu_.id_, hw_cpu_.name_, hw_cpu_.num_cores_,
         NumFormatter::FormatStorageSize(memory_size_));
    for (const auto& disk : hw_disks_) {
        LOGI("hardware disk name={} model={} status={} serial={} interface={}", disk.name_, disk.model_, disk.status_, disk.serial_number_,
             disk.interface_type_);
    }
    for (const auto& gpu : gpus_) LOGI("hardware gpu name={} memory={} pnp={}", gpu.name_, gpu.size_str_, gpu.pnp_device_id_);
    LOGI("hardware mac={}", mac_address_);
}

std::string Hardware::GetHardwareDescription() const {
    std::stringstream stream{};
    for (const auto& disk : hw_disks_) stream << disk.serial_number_;
    stream << mac_address_;
    auto description = stream.str();
    StringUtil::Replace(description, " ", "");
    return description;
}

void Hardware::DetectMac() {
    ULONG buffer_size{0};
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &buffer_size) != ERROR_BUFFER_OVERFLOW) return;
    std::vector<std::byte> buffer(buffer_size);
    auto adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());  // NOLINT(gammaray-raw-pointer-boundary): Win32 linked-list ABI.
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &buffer_size) != ERROR_SUCCESS) return;
    for (auto current = adapter; current != nullptr; current = current->Next) {  // NOLINT(gammaray-raw-pointer-boundary): borrowed Win32 list nodes.
        if (current->OperStatus != IfOperStatusUp || current->IfType == IF_TYPE_SOFTWARE_LOOPBACK || current->PhysicalAddressLength == 0) continue;
        std::string mac{};
        for (DWORD index = 0; index < current->PhysicalAddressLength; ++index) {
            if (index > 0) mac.push_back(':');
            mac += std::format("{:02x}", static_cast<unsigned int>(current->PhysicalAddress[index]));
        }
        mac_address_ = std::move(mac);
        break;
    }
}

std::string Hardware::GetDesktopName() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> name{};
    DWORD size = static_cast<DWORD>(name.size());
    return GetComputerNameW(name.data(), &size) ? StringUtil::ToUTF8(name.data()) : std::string{};
}

void Hardware::LockScreen() {
    static_cast<void>(LockWorkStation());
}

bool Hardware::AcquirePermissionForRestartDevice() {
    HANDLE token{};  // NOLINT(gammaray-raw-pointer-boundary): OpenProcessToken transfers a HANDLE through HANDLE*.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    const UniqueWinHandle owned_token{token};
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) return false;
    return AdjustTokenPrivileges(owned_token.get(), FALSE, &privileges, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
}

void Hardware::RestartDevice() {
    if (!AcquirePermissionForRestartDevice() || !ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, 0)) {
        LOGE("event=hardware.power code=RESTART_FAILED operation=restart outcome=failed recoverable=true win32_error={}", GetLastError());
    }
}

void Hardware::ShutdownDevice() {
    if (!AcquirePermissionForRestartDevice() || !ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, 0)) {
        LOGE("event=hardware.power code=SHUTDOWN_FAILED operation=shutdown outcome=failed recoverable=true win32_error={}", GetLastError());
    }
}

}  // namespace px
