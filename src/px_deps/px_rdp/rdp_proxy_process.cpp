#include "rdp_proxy_process.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <wincrypt.h>
#include <iphlpapi.h>
#include <tcpmib.h>

namespace px::rdp {
namespace {

bool Identifier(std::string_view value) {
    return !value.empty() && value.size() <= 128 && std::ranges::all_of(value, [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
    });
}

bool Pin(std::string_view value) {
    return value.size() == 64 && std::ranges::all_of(value, [](unsigned char byte) {
               return (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F') || (byte >= '0' && byte <= '9');
           });
}

struct LocalBlobCloser final {
    std::size_t size{};
    void operator()(unsigned char* bytes) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): LocalFree allocation ABI.
        if (bytes) {
            SecureZeroMemory(bytes, size);
            LocalFree(bytes);
        }
    }
};

class PrivateBytes final {
  public:
    explicit PrivateBytes(std::size_t size) : bytes_(size) {}
    PrivateBytes(const PrivateBytes&) = delete;
    PrivateBytes& operator=(const PrivateBytes&) = delete;
    ~PrivateBytes() {
        if (!bytes_.empty()) {
            SecureZeroMemory(bytes_.data(), bytes_.size());
        }
    }
    [[nodiscard]] std::span<unsigned char> Bytes() {
        return bytes_;
    }

  private:
    std::vector<unsigned char> bytes_{};
};

std::unique_ptr<PrivateBytes> Unseal(const std::filesystem::path& path, const std::string& binding) {
    const auto file = UniqueWinHandle{
        CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) || info.nFileSizeHigh != 0 || info.nFileSizeLow == 0 || info.nFileSizeLow > 128 * 1024 ||
        info.nNumberOfLinks != 1 || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return {};
    }
    auto encrypted = std::vector<unsigned char>(info.nFileSizeLow);
    DWORD read{};
    if (!ReadFile(file.get(), encrypted.data(), static_cast<DWORD>(encrypted.size()), &read, nullptr) || read != encrypted.size()) {
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
    DATA_BLOB entropy{static_cast<DWORD>(binding.size()), reinterpret_cast<BYTE*>(const_cast<char*>(binding.data()))};
    DATA_BLOB output{}; // NOLINT(gammaray-raw-pointer-boundary): synchronous DPAPI out value, wrapped immediately below.
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    auto owned = std::unique_ptr<unsigned char, LocalBlobCloser>{output.pbData, LocalBlobCloser{output.cbData}};
    if (!owned || output.cbData == 0 || output.cbData > 64 * 1024) {
        if (owned) {
            SecureZeroMemory(owned.get(), output.cbData);
        }
        return {};
    }
    auto plain = std::make_unique<PrivateBytes>(output.cbData);
    std::ranges::copy(std::span<const unsigned char>{owned.get(), output.cbData}, plain->Bytes().begin());
    SecureZeroMemory(owned.get(), output.cbData);
    return plain;
}

bool WritePrivateConfiguration(const std::filesystem::path& path, std::span<const unsigned char> bytes, bool& created) {
    created = false;
    const auto file = UniqueWinHandle{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || file.get() == INVALID_HANDLE_VALUE) {
        return false;
    }
    created = true;
    DWORD written{};
    return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() &&
           FlushFileBuffers(file.get());
}

struct EnvironmentCloser final {
    void operator()(wchar_t* value) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 environment allocation ABI.
        if (value) {
            FreeEnvironmentStringsW(value);
        }
    }
};

std::vector<wchar_t> ProxyEnvironment(const RdpProxyLaunch& launch) {
    const auto existing = std::unique_ptr<wchar_t, EnvironmentCloser>{GetEnvironmentStringsW()};
    if (!existing) {
        return {};
    }
    std::vector<std::wstring> entries{};
    std::size_t offset{};
    while (existing.get()[offset] != L'\0') {
        const auto entry = std::wstring_view{existing.get() + offset};
        offset += entry.size() + 1;
        auto upper = std::wstring(entry);
        std::ranges::transform(upper, upper.begin(), [](wchar_t value) { return value >= L'a' && value <= L'z' ? value - (L'a' - L'A') : value; });
        if (!upper.starts_with(L"WINPR_NATIVE_SSPI=") && !upper.starts_with(L"OPENSSL_MODULES=") &&
            !upper.starts_with(L"GAMMARAY_RDP_TARGET_CERT_SHA256=") && !upper.starts_with(L"GAMMARAY_RDP_AUDIT_PATH=")) {
            entries.emplace_back(entry);
        }
    }
    entries.emplace_back(L"WINPR_NATIVE_SSPI=1");
    entries.emplace_back(L"OPENSSL_MODULES=" + launch.proxy_directory.wstring());
    entries.emplace_back(L"GAMMARAY_RDP_TARGET_CERT_SHA256=" +
                         std::wstring(launch.target_certificate_sha256.begin(), launch.target_certificate_sha256.end()));
    entries.emplace_back(L"GAMMARAY_RDP_AUDIT_PATH=" + (launch.private_root / (launch.workspace_id + ".audit.log")).wstring());
    std::ranges::sort(entries);
    std::vector<wchar_t> environment{};
    for (const auto& entry : entries) {
        environment.insert(environment.end(), entry.begin(), entry.end());
        environment.push_back(L'\0');
    }
    environment.push_back(L'\0');
    return environment;
}

bool OwnsListener(DWORD pid, std::uint16_t port) {
    DWORD size{};
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != ERROR_INSUFFICIENT_BUFFER ||
        size < sizeof(MIB_TCPTABLE_OWNER_PID) || size > 16 * 1024 * 1024) {
        return false;
    }
    auto bytes = std::vector<unsigned char>(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) {
        return false;
    }
    const auto& table = *reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
    if (table.dwNumEntries > (bytes.size() - sizeof(DWORD)) / sizeof(MIB_TCPROW_OWNER_PID)) {
        return false;
    }
    for (DWORD index{}; index < table.dwNumEntries; ++index) {
        const auto& row = table.table[index];
        const auto local_port = static_cast<std::uint16_t>(((row.dwLocalPort & 255) << 8) | ((row.dwLocalPort >> 8) & 255));
        if (row.dwOwningPid == pid && row.dwLocalAddr == 0x0100007f && local_port == port) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string RdpProxyLaunch::Entropy() const {
    if (!Identifier(workspace_id) || !Identifier(instance_id) || !Identifier(node_id) || !Identifier(device_id) || proxy_port == 0 ||
        !Pin(target_certificate_sha256) || !Pin(proxy_certificate_sha256)) {
        return {};
    }
    return std::format("GammaRay.RdpBootstrap.v1|{}|{}|{}|{}|{}|{}|{}", workspace_id, instance_id, node_id, device_id, proxy_port,
                       target_certificate_sha256, proxy_certificate_sha256);
}

RdpProxyProcess::RdpProxyProcess(ConstructionKey, std::unique_ptr<RdpWorkspaceLease> lease, UniqueWinHandle job, UniqueWinHandle process,
                                 std::filesystem::path configuration)
    : lease_(std::move(lease)), job_(std::move(job)), process_(std::move(process)), configuration_(std::move(configuration)) {}

RdpProxyProcess::~RdpProxyProcess() {
    Stop();
}

std::unique_ptr<RdpProxyProcess> RdpProxyProcess::Start(const RdpProxyLaunch& launch, std::string& error) {
    error = "RDP bootstrap identity or deployment invalid";
    const auto entropy = launch.Entropy();
    if (entropy.empty() || !launch.proxy_directory.is_absolute() || !launch.private_root.is_absolute()) {
        return {};
    }
    const auto executable = launch.proxy_directory / "freerdp-proxy.exe";
    if (!std::filesystem::is_regular_file(executable) ||
        !std::filesystem::is_regular_file(launch.proxy_directory / "proxy" / "proxy-gammaray-policy-plugin.dll")) {
        return {};
    }
    auto lease = RdpWorkspaceLease::Acquire(launch.private_root, launch.workspace_id);
    if (!lease) {
        error = "RDP workspace is busy or its runtime lease is inaccessible";
        return {};
    }
    const auto bootstrap = launch.private_root / (launch.instance_id + ".bootstrap");
    const auto configuration = launch.private_root / (launch.instance_id + ".proxy.ini");
    auto plain = Unseal(bootstrap, entropy);
    if (!plain) {
        error = "RDP Service-token bootstrap authentication failed";
        return {};
    }
    auto owner = std::make_unique<RdpProxyProcess>(ConstructionKey{}, std::move(lease), UniqueWinHandle{}, UniqueWinHandle{}, configuration);
    bool configuration_created{};
    if (!WritePrivateConfiguration(configuration, plain->Bytes(), configuration_created)) {
        error = "RDP private proxy configuration creation failed";
        // A pre-existing configuration belongs to another start and must not be deleted.
        if (!configuration_created) {
            owner->configuration_.clear();
        }
        return {};
    }
    plain.reset();
    if (!DeleteFileW(bootstrap.c_str())) {
        error = "RDP encrypted bootstrap cleanup failed";
        return {};
    }
    owner->job_ = UniqueWinHandle{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!owner->job_ || !SetInformationJobObject(owner->job_.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error = "RDP proxy supervision job creation failed";
        return {};
    }
    auto environment = ProxyEnvironment(launch);
    auto command = L"\"" + executable.wstring() + L"\" \"" + configuration.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION output{}; // NOLINT(gammaray-raw-pointer-boundary): Win32 synchronous out handles, immediately RAII-wrapped.
    if (environment.empty() ||
        !CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        environment.data(), launch.proxy_directory.c_str(), &startup, &output)) {
        error = "RDP proxy process creation failed";
        return {};
    }
    owner->process_ = UniqueWinHandle{output.hProcess};
    auto thread = UniqueWinHandle{output.hThread};
    if (!AssignProcessToJobObject(owner->job_.get(), owner->process_.get())) {
        static_cast<void>(TerminateProcess(owner->process_.get(), 1));
        error = "RDP proxy process supervision failed";
        return {};
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        error = "RDP proxy process resume failed";
        return {};
    }
    for (int attempt{}; attempt < 100; ++attempt) {
        if (WaitForSingleObject(owner->process_.get(), 50) != WAIT_TIMEOUT) {
            break;
        }
        if (OwnsListener(output.dwProcessId, launch.proxy_port)) {
            // The pinned proxy has parsed configuration before binding its
            // listener. Do not retain plaintext credentials for its lifetime.
            if (!DeleteFileW(configuration.c_str())) {
                error = "RDP proxy started but private configuration removal failed";
                return {};
            }
            owner->configuration_.clear();
            error.clear();
            return owner;
        }
    }
    error = "RDP proxy did not establish its protected loopback listener within five seconds";
    return {};
}

void RdpProxyProcess::Stop() noexcept {
    std::lock_guard lock(mutex_);
    job_.reset();
    if (process_) {
        static_cast<void>(WaitForSingleObject(process_.get(), 3000));
        process_.reset();
    }
    if (!configuration_.empty()) {
        static_cast<void>(DeleteFileW(configuration_.c_str()));
        configuration_.clear();
    }
    lease_.reset();
}

bool RdpProxyProcess::IsAlive() const noexcept {
    std::lock_guard lock(mutex_);
    return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
}

} // namespace px::rdp
#endif
