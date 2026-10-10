#include "panel_client_launcher.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>

#include "panel_connection_links.h"
#include "px_common/log.h"
#include "px_common/uuid.h"

namespace px::panel::product {
namespace {

std::string_view PlatformName(const px::ui::DevicePlatform platform) noexcept {
    switch (platform) {
        case px::ui::DevicePlatform::Windows:
            return "windows";
        case px::ui::DevicePlatform::MacOS:
            return "macos";
        case px::ui::DevicePlatform::Android:
            return "android";
        case px::ui::DevicePlatform::IOS:
            return "ios";
        case px::ui::DevicePlatform::Unknown:
            return "unknown";
    }
    return "unknown";
}

class WinHandle final {
public:
    WinHandle() = default;
    explicit WinHandle(const HANDLE value) : value_{value} {}
    ~WinHandle() { Reset(); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    WinHandle(WinHandle&& other) noexcept : value_{std::exchange(other.value_, nullptr)} {}
    WinHandle& operator=(WinHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const { return value_; }
    [[nodiscard]] bool Valid() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void Reset(const HANDLE value = nullptr) {
        if (Valid()) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_{};
};

std::wstring Quote(std::wstring value) {
    std::wstring result{L"\""};
    std::size_t slashes{};
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(character);
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring MakeCommandLine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
    std::wstring command{Quote(executable.wstring())};
    for (const auto& argument : arguments) command += L" " + Quote(argument);
    return command;
}

}  // namespace

struct PanelClientLauncher::Process final {
    WinHandle handle{};
    DWORD id{};
};

std::shared_ptr<PanelClientLauncher> PanelClientLauncher::Create(const std::shared_ptr<PanelConfigStore>& config,
                                                                 const std::shared_ptr<PanelLocalServer>& localServer) {
    return std::make_shared<PanelClientLauncher>(config, localServer);
}

PanelClientLauncher::PanelClientLauncher(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelLocalServer> localServer)
    : config_{std::move(config)}, localServer_{std::move(localServer)} {}
PanelClientLauncher::~PanelClientLauncher() { Shutdown(); }

ClientLaunchResult PanelClientLauncher::Launch(const NativeLaunchRequest& originalRequest) {
    auto request = originalRequest;
    if (request.directHost.empty() || request.directPort <= 0 || request.directPort > 65535 || request.directStreamId.empty() ||
        request.nonce.empty()) {
        return false;
    }
    if (stopping_.load() || !localServer_) return false;
    request.panelLaunchId = px::GetCanonicalUUID();
    request.panelPort = localServer_->Snapshot().listenPort;
    const auto startup = std::make_shared<PanelClientStartup>(request.directStreamId);
    if (!localServer_->RegisterClientStartup(request.panelLaunchId, startup)) return false;
    struct Registration final {
        std::shared_ptr<PanelLocalServer> server{};
        std::string launchId{};
        ~Registration() { server->ForgetClientStartup(launchId); }
    } registration{localServer_, request.panelLaunchId};
    const auto process = request.connectionKind == NativeConnectionKind::Rdp ? LaunchRdp(request, request.directHost, request.directPort)
                                                                             : LaunchNative(request, request.directHost, request.directPort);
    if (!process) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
    ClientLaunchResult result{px::ui::TextId::ConnectionClientConnectTimeout};
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto reported = startup->WaitFor(std::chrono::milliseconds{50})) {
            result = *reported;
            break;
        }
        if (stopping_.load() || !process || WaitForSingleObject(process->handle.Get(), 0) != WAIT_TIMEOUT) {
            result = px::ui::TextId::ConnectionClientExited;
            break;
        }
    }
    LOGI("event=client.startup component=panel launch={} stream={} outcome={} error={}", request.panelLaunchId, request.directStreamId,
         result.connected ? "connected" : "failed", result.connected ? 0 : static_cast<int>(result.error));
    if (!result && process && process->handle.Valid()) {
        // Cancel only this exact child, never a reused stream/PID or the remote application.
        static_cast<void>(TerminateProcess(process->handle.Get(), 1));
        const std::scoped_lock lock{mutex_};
        const auto found = processes_.find(request.directStreamId);
        if (found != processes_.end() && found->second == process) processes_.erase(found);
    }
    return result;
}

namespace {

nlohmann::json BuildNativeEnvelope(const NativeLaunchRequest& request, const std::string& host, const int port, const PanelConfigStore& config) {
    const auto identity = config.Identity();
    const auto settings = config.Settings();
    const auto console = config.Console();
    const std::string localHost{ResolveNodeAccessHost({}, CollectPanelLocalAddresses())};
    const std::array decoderNames{"Auto", "Hardware", "Software"};
    return {{"schema", 1},
            {"panel_port", request.panelPort},
            {"panel_launch_id", request.panelLaunchId},
            {"mode", request.fileTransfer ? "file-transfer" : "desktop"},
            {"host", host},
            {"local_host", localHost},
            {"port", port},
            {"iroh", px::IrohConnectionDescriptionJson(request.iroh)},
            {"appkey", request.relayAdmissionTicket},
            {"stream_id", request.directStreamId},
            {"stream_name", request.displayName},
            {"connection_instance_id", request.instanceId},
            {"connection_nonce", request.nonce},
            {"device_id", identity.deviceId},
            {"remote_device_id", request.remoteDeviceId},
            {"console_origin", console ? console->baseUrl : std::string{}},
            {"remote_platform", PlatformName(request.remotePlatform)},
            {"remote_password_hash", request.remotePasswordHash},
            {"frontend_session_id", request.frontendSessionId},
            {"frontend_session_revision", request.frontendSessionRevision},
            {"frontend_token", request.frontendToken ? std::string{request.frontendToken->View()} : std::string{}},
            {"language", settings.language == ::px::ui::Language::English ? "en-US" : "zh-CN"},
            {"theme", settings.theme == ::px::ui::Theme::Light ? "light" : "dark"},
            {"decoder", request.forceSoftware ? "Software" : decoderNames[static_cast<std::size_t>(settings.controller.preferredDecoder)]},
            {"recording_path", settings.controller.recordingPath},
            {"only_viewing", request.viewOnly},
            {"audio", !request.viewOnly && request.audio},
            {"clipboard", !request.viewOnly && request.clipboard},
            {"force_tcp", request.forceTcp},
            {"force_relay", request.forceRelay},
            {"split_windows", request.splitWindows},
            {"force_gdi_capture", request.forceGdiCapture},
            {"disable_vulkan_render", request.disableVulkan},
            {"wait_debug", request.waitForDebugger},
            {"relay_host", request.relayHost},
            {"relay_port", request.relayPort},
            {"relay_remote_device_id", request.relayRemoteDeviceId}};
}

}  // namespace

std::shared_ptr<PanelClientLauncher::Process> PanelClientLauncher::LaunchNative(const NativeLaunchRequest& request, const std::string& host, const int port) {
    const auto executable = config_->ExecutableDirectory() / "px_client.exe";
    if (!std::filesystem::exists(executable)) return {};
    std::string envelope{BuildNativeEnvelope(request, host, port, *config_).dump()};
    SECURITY_ATTRIBUTES security{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    HANDLE readPipe{};   // NOLINT(pixels-raw-pointer-boundary): CreatePipe boundary, immediately wrapped
    HANDLE writePipe{};  // NOLINT(pixels-raw-pointer-boundary): CreatePipe boundary, immediately wrapped
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return {};
    WinHandle childInput{readPipe};
    WinHandle parentInput{writePipe};
    static_cast<void>(SetHandleInformation(parentInput.Get(), HANDLE_FLAG_INHERIT, 0));
    auto command = MakeCommandLine(executable, {L"--native-launch-stdin"});
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput.Get();
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                        config_->ExecutableDirectory().c_str(), &startup, &processInfo)) {
        LOGE("Native client failed to start: {}", GetLastError());
        return {};
    }
    CloseHandle(processInfo.hThread);
    childInput.Reset();
    DWORD written{};
    const bool sent =
        WriteFile(parentInput.Get(), envelope.data(), static_cast<DWORD>(envelope.size()), &written, nullptr) != FALSE && written == envelope.size();
    if (!envelope.empty()) SecureZeroMemory(envelope.data(), envelope.size());
    parentInput.Reset();
    if (!sent) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        return {};
    }
    const auto process = std::make_shared<Process>();
    process->handle = WinHandle{processInfo.hProcess};
    process->id = processInfo.dwProcessId;
    const std::scoped_lock lock{mutex_};
    processes_[request.directStreamId] = process;
    return process;
}

std::shared_ptr<PanelClientLauncher::Process> PanelClientLauncher::LaunchRdp(const NativeLaunchRequest& request, const std::string& host,
                                                                             const int port) {
    if (request.viewOnly || !request.rdpConfiguration) return {};
    const auto executable = config_->ExecutableDirectory() / "px_client.exe";
    if (!std::filesystem::exists(executable)) return {};
    const auto settings = config_->Settings();
    const auto console = config_->Console();
    nlohmann::json launch{{"schema", 1},
                          {"panel_port", request.panelPort},
                          {"panel_launch_id", request.panelLaunchId},
                          {"host", host},
                          {"port", port},
                          {"stream_id", request.directStreamId},
                          {"connection_nonce", request.nonce},
                          {"connection_instance_id", request.instanceId},
                          {"device_id", config_->Identity().deviceId},
                          {"remote_device_id", request.remoteDeviceId},
                          {"console_origin", console ? console->baseUrl : std::string{}},
                          {"remote_password_hash", request.remotePasswordHash},
                          {"frontend_session_id", request.frontendSessionId},
                          {"frontend_session_revision", request.frontendSessionRevision},
                          {"frontend_token", request.frontendToken ? std::string{request.frontendToken->View()} : std::string{}},
                          {"language", settings.language == ::px::ui::Language::English ? "en-US" : "zh-CN"},
                          {"theme", settings.theme == ::px::ui::Theme::Light ? "light" : "dark"},
                          {"audio", true},
                          {"clipboard", true},
                          {"rdp", nlohmann::json::parse(request.rdpConfiguration->View())}};
    std::string envelope{launch.dump()};
    SECURITY_ATTRIBUTES security{.nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    HANDLE readPipe{};   // NOLINT(pixels-raw-pointer-boundary): CreatePipe boundary, immediately wrapped
    HANDLE writePipe{};  // NOLINT(pixels-raw-pointer-boundary): CreatePipe boundary, immediately wrapped
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return {};
    WinHandle childInput{readPipe};
    WinHandle parentInput{writePipe};
    static_cast<void>(SetHandleInformation(parentInput.Get(), HANDLE_FLAG_INHERIT, 0));
    auto command = MakeCommandLine(executable, {L"--rdp-launch-stdin"});
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput.Get();
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr,
                        config_->ExecutableDirectory().c_str(), &startup, &processInfo)) {
        return {};
    }
    CloseHandle(processInfo.hThread);
    childInput.Reset();
    DWORD written{};
    const bool sent =
        WriteFile(parentInput.Get(), envelope.data(), static_cast<DWORD>(envelope.size()), &written, nullptr) != FALSE && written == envelope.size();
    if (!envelope.empty()) SecureZeroMemory(envelope.data(), envelope.size());
    parentInput.Reset();
    if (!sent) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        return {};
    }
    const auto process = std::make_shared<Process>();
    process->handle = WinHandle{processInfo.hProcess};
    process->id = processInfo.dwProcessId;
    const std::scoped_lock lock{mutex_};
    processes_[request.directStreamId] = process;
    return process;
}

bool PanelClientLauncher::Stop(const std::string& streamId) {
    std::shared_ptr<Process> process{};
    {
        const std::scoped_lock lock{mutex_};
        const auto found = processes_.find(streamId);
        if (found == processes_.end()) return false;
        process = std::move(found->second);
        processes_.erase(found);
    }
    return !process->handle.Valid() || TerminateProcess(process->handle.Get(), 0) != FALSE;
}

void PanelClientLauncher::Shutdown() {
    stopping_.store(true);
    StopAll();
}

void PanelClientLauncher::StopAll() {
    if (localServer_) localServer_->CancelClientStartups();
    std::unordered_map<std::string, std::shared_ptr<Process>> processes{};
    {
        const std::scoped_lock lock{mutex_};
        processes.swap(processes_);
    }
    for (const auto& [streamId, process] : processes) {
        static_cast<void>(streamId);
        if (process && process->handle.Valid()) static_cast<void>(TerminateProcess(process->handle.Get(), 0));
    }
}

}  // namespace px::panel::product
