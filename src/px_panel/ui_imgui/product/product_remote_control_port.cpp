#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "connection_progress_tracker.h"
#include "panel_connection_input.h"
#include "panel_connection_links.h"
#include "panel_credential_vault.h"
#include "panel_product_runtime.h"
#include "px_common/http_client.h"
#include "px_common/uuid.h"
#include "px_console_client/console_api.h"
#include "px_console_client/console_user_device.h"
#include "render_api.h"

namespace px::panel::product {
namespace {

enum class DirectSessionMode : std::uint8_t {
    Control,
    ViewOnly,
    FileTransfer,
};

ui::ConnectionIntent ConnectionIntentFor(const DirectSessionMode mode) {
    switch (mode) {
        case DirectSessionMode::ViewOnly:
            return ui::ConnectionIntent::ViewOnly;
        case DirectSessionMode::FileTransfer:
            return ui::ConnectionIntent::FileTransfer;
        case DirectSessionMode::Control:
        default:
            return ui::ConnectionIntent::Control;
    }
}

std::string_view DeviceCommandEvent(const ui::RemoteDeviceCommand command) {
    switch (command) {
        case ui::RemoteDeviceCommand::Lock:
            return "lock_screen";
        case ui::RemoteDeviceCommand::Restart:
            return "restart_device";
        case ui::RemoteDeviceCommand::Shutdown:
            return "shutdown_device";
    }
    return {};
}

std::string_view DeviceCommandName(const ui::RemoteDeviceCommand command) {
    switch (command) {
        case ui::RemoteDeviceCommand::Lock:
            return "Lock screen";
        case ui::RemoteDeviceCommand::Restart:
            return "Restart";
        case ui::RemoteDeviceCommand::Shutdown:
            return "Shutdown";
    }
    return "Device command";
}

bool SendDirectDeviceCommand(const std::string& host, const int port, const std::string& payload) {
    if (host.empty() || port <= 0 || port > 65535) return false;
    const auto client = HttpClient::Make(host, port, "/panel/stream/message", 3000);
    if (!client) return false;
    const auto response = client->Post({}, payload, "application/json");
    if (response.status != 200 || response.body.empty()) return false;
    try {
        return nlohmann::json::parse(response.body).value("code", 0) == 200;
    } catch (...) {
        return false;
    }
}

class ProductRemoteControlPort final : public ui::RemoteControlPort, public std::enable_shared_from_this<ProductRemoteControlPort> {
public:
    struct RefreshLoopState final {
        std::mutex mutex{};
        std::condition_variable_any wakeup{};
    };

    explicit ProductRemoteControlPort(std::shared_ptr<PanelProductRuntime> runtime)
        : runtime_{std::move(runtime)}, credentialVault_{PanelCredentialVault::Create()} {}
    ~ProductRemoteControlPort() override {
        refreshThread_.request_stop();
        refreshLoopState_->wakeup.notify_all();
        if (refreshThread_.joinable()) refreshThread_.join();
    }
    void Initialize() {
        showPassword_.store(runtime_->Config()->ShowTemporaryPassword(), std::memory_order_release);
        RefreshDevices();
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        const auto loopState = refreshLoopState_;
        refreshThread_ = std::jthread{[weakSelf, loopState](const std::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                {
                    std::unique_lock lock{loopState->mutex};
                    loopState->wakeup.wait_for(lock, stopToken, std::chrono::seconds{2}, [] { return false; });
                }
                if (stopToken.stop_requested()) return;
                const auto self = weakSelf.lock();
                if (!self) return;
                self->RefreshDevices();
            }
        }};
    }

    ui::RemoteControlState Snapshot() const override {
        const auto identity = runtime_->Config()->Identity();
        const auto ports = runtime_->Config()->Ports();
        const auto localAddresses = CollectPanelLocalAddresses();
        const auto service = runtime_->Service();
        const std::string nodeAccessHost{service ? service->Snapshot().nodeAccessHost : std::string{}};
        const auto links = BuildPanelConnectionLinks(identity, ports, nodeAccessHost, localAddresses);
        ui::RemoteControlState result{.deviceId = identity.deviceId,
                                      .temporaryPassword = identity.randomPassword,
                                      .deviceName = identity.deviceName,
                                      .desktopLink = links.desktop,
                                      .webClientAddress = links.web,
                                      .showTemporaryPassword = showPassword_,
                                      .incomingRemoteAccessEnabled = runtime_->Config()->IncomingRemoteAccessEnabled(),
                                      .managerOnline = managerOnline_.load(std::memory_order_acquire)};
        {
            const std::scoped_lock lock{mutex_};
            result.devices = devices_;
        }
        return result;
    }

    std::optional<ui::ConnectionProgress> ConnectionProgressSnapshot() const override { return connectionProgress_.Snapshot(); }

    void SetPasswordVisible(const bool visible) override {
        showPassword_ = visible;
        static_cast<void>(runtime_->Config()->SaveShowTemporaryPassword(visible));
    }
    void SetIncomingRemoteAccessEnabled(const bool enabled) override {
        if (!runtime_->Config()->SaveIncomingRemoteAccessEnabled(enabled)) {
            runtime_->Notify(true, "Remote access", "The remote access setting could not be saved.");
            return;
        }
        runtime_->LocalServer()->RefreshPanelInfo();
    }
    void UpdateLocalDeviceName(std::string deviceName) override {
        const auto first = deviceName.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            runtime_->Notify(true, "Device name", "Enter a non-empty device name.");
            return;
        }
        const auto last = deviceName.find_last_not_of(" \t\r\n");
        deviceName = deviceName.substr(first, last - first + 1);
        const auto runtime = runtime_;
        static_cast<void>(runtime_->Worker()->Post([runtime, deviceName = std::move(deviceName)] {
            if (!runtime->Config()->SaveCustomDeviceName(deviceName)) {
                runtime->Notify(true, "Device name", "The local device name could not be saved.");
                return;
            }
            if (const auto service = runtime->Service(); service && !service->RestartRender()) {
                runtime->Notify(true, "Device name", "The name was saved, but the Render service could not be restarted.");
                return;
            }
            runtime->Notify(false, "Device name", "Local device name updated.");
        }));
    }
    void Refresh() override { RefreshDevices(); }
    void RefreshTemporaryPassword() override {
        const auto runtime = runtime_;
        static_cast<void>(runtime_->Worker()->Post([runtime] {
            auto identity = runtime->Config()->Identity();
            identity.randomPassword = px::GetUUID();
            if (!runtime->Config()->SaveIdentity(identity)) {
                runtime->Notify(true, "Password", "The new temporary password could not be saved locally.");
                return;
            }
            if (const auto service = runtime->Service(); service && !service->RestartRender()) {
                runtime->Notify(true, "Password", "The password was updated, but the Render service could not be restarted.");
                return;
            }
            runtime->Notify(false, "Password", "Temporary password refreshed.");
        }));
    }

    bool RequiresPassword(const std::string& target) const override {
        const auto parsed = ParseConnectionInput(target, runtime_->Config()->Ports().desktop);
        if (!parsed || parsed->kind == ConnectionInputKind::SharedLink) {
            return false;
        }
        if (parsed->kind == ConnectionInputKind::DeviceId && runtime_->Console()->Account().loggedIn) {
            return false;
        }
        return !credentialVault_->Read(CredentialKey(*parsed)).has_value();
    }

    void Connect(std::string target, std::string password, const bool viewOnly) override {
        QueueConnectionInput(std::move(target), std::move(password), viewOnly ? DirectSessionMode::ViewOnly : DirectSessionMode::Control);
    }

    void StartStream(const std::string& streamId, const bool viewOnly) override {
        std::optional<ui::RemoteDeviceCard> target{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId); found != devices_.end()) target = *found;
        }
        const auto mode{viewOnly ? DirectSessionMode::ViewOnly : DirectSessionMode::Control};
        if (!target) {
            ReportImmediateFailure(streamId, mode, ui::ConnectionFailureReason::DeviceResolutionFailed,
                                   "The selected device is no longer in the device list. Refresh the list and retry.");
            return;
        }
        if (target->host.empty()) {
            QueueConnectionInput(target->deviceId, {}, mode);
            return;
        }
        QueueResolvedConnection({.kind = ConnectionInputKind::DirectEndpoint,
                                 .deviceId = target->deviceId,
                                 .displayName = target->name,
                                 .platform = target->platform,
                                 .hosts = {target->host},
                                 .port = target->port},
                                mode);
    }
    void StartFileTransfer(const std::string& streamId, std::string password) override {
        std::optional<ui::RemoteDeviceCard> target{};
        {
            const std::scoped_lock lock{mutex_};
            const auto found = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId);
            if (found == devices_.end()) {
                ReportImmediateFailure(streamId, DirectSessionMode::FileTransfer, ui::ConnectionFailureReason::DeviceResolutionFailed,
                                       "The selected device is no longer in the device list. Refresh the list and retry.");
            } else {
                target = *found;
            }
        }
        if (!target) return;

        ParsedConnectionInput direct{.kind = ConnectionInputKind::DirectEndpoint,
                                     .deviceId = target->deviceId,
                                     .displayName = target->name,
                                     .platform = target->platform,
                                     .hosts = target->host.empty() ? std::vector<std::string>{} : std::vector<std::string>{target->host},
                                     .port = target->port,
                                     .password = std::move(password)};
        if (direct.hosts.empty()) {
            QueueConnectionInput(target->deviceId, std::move(direct.password), DirectSessionMode::FileTransfer);
            return;
        }
        QueueResolvedConnection(std::move(direct), DirectSessionMode::FileTransfer);
    }

    void StopStream(const std::string& streamId) override {
        ActiveSession activeSession{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = activeSessions_.find(streamId); found != activeSessions_.end()) {
                activeSession = std::move(found->second);
                activeSessions_.erase(found);
            }
        }
        if (!activeSession.id.empty()) {
            static_cast<void>(runtime_->Launcher()->Stop(activeSession.id));
            if (activeSession.revision > 0) {
                const auto runtime = runtime_;
                static_cast<void>(runtime_->Worker()->Post([runtime, activeSession = std::move(activeSession)] {
                    static_cast<void>(runtime->Console()->CloseResourceConnection(activeSession.id, activeSession.revision));
                }));
            }
        }
    }

    void SendDeviceCommand(const std::string& streamId, const ui::RemoteDeviceCommand command) override {
        ui::RemoteDeviceCard target{};
        {
            const std::scoped_lock lock{mutex_};
            const auto found = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId);
            if (found == devices_.end()) {
                runtime_->Notify(true, "Device command", "The selected device is no longer in the device list. Refresh and retry.");
                return;
            }
            target = *found;
        }
        if (!target.online) {
            runtime_->Notify(true, std::string{DeviceCommandName(command)}, "The selected device is offline. Refresh its status and retry.");
            return;
        }

        const auto runtime = runtime_;
        const bool queued = runtime_->Worker()->Post([runtime, target = std::move(target), command] {
            const auto identity = runtime->Config()->Identity();
            const std::string payload{nlohmann::json{{"event", DeviceCommandEvent(command)}, {"from_device", identity.deviceId}}.dump()};
            const bool delivered{SendDirectDeviceCommand(target.host, target.port, payload)};
            runtime->Notify(!delivered, std::string{DeviceCommandName(command)},
                            delivered ? "The command was sent to the device."
                                      : "The current Console directory does not expose a command endpoint for this device.");
        });
        if (!queued) runtime_->Notify(true, std::string{DeviceCommandName(command)}, "The command could not be queued. Please retry.");
    }

    void DeleteDevice(const std::string& streamId) override {
        StopStream(streamId);
        std::string deviceId{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId); found != devices_.end())
                deviceId = found->deviceId;
            std::erase_if(devices_, [&streamId](const ui::RemoteDeviceCard& value) { return value.streamId == streamId; });
        }
        static_cast<void>(runtime_->Config()->HideRemoteDevice(deviceId));
        static_cast<void>(runtime_->Config()->DeleteRemoteDevicePreference(deviceId));
        static_cast<void>(runtime_->Config()->DeleteRemoteDeviceHistory(deviceId));
    }

    void SaveDevice(ui::RemoteDeviceCard device) override {
        const RemoteDevicePreference preference{.name = device.name,
                                                .audio = device.audio,
                                                .clipboard = device.clipboard,
                                                .viewOnly = device.viewOnly,
                                                .splitWindows = device.splitWindows,
                                                .forceSoftware = device.forceSoftware,
                                                .forceTcp = device.forceTcp,
                                                .forceRelay = device.forceRelay,
                                                .waitForDebugger = device.waitForDebugger,
                                                .forceGdiCapture = device.forceGdiCapture,
                                                .disableVulkan = device.disableVulkan};
        if (!runtime_->Config()->SaveRemoteDevicePreference(device.deviceId, preference)) {
            runtime_->Notify(true, "Pixels", "Unable to save device settings");
            return;
        }
        const auto history = runtime_->Config()->LoadRemoteDeviceHistory();
        if (const auto saved = std::ranges::find(history, device.deviceId, &RemoteDeviceHistory::deviceId); saved != history.end()) {
            auto renamed = *saved;
            renamed.name = device.name;
            static_cast<void>(runtime_->Config()->SaveRemoteDeviceHistory(renamed));
        }
        const std::scoped_lock lock{mutex_};
        if (const auto found = std::ranges::find(devices_, device.streamId, &ui::RemoteDeviceCard::streamId); found != devices_.end())
            *found = std::move(device);
    }

    void CopyText(const std::string& text) override {
        if (text.empty() || !SDL_SetClipboardText(text.c_str())) {
            runtime_->Notify(true, "Pixels", "Unable to copy this value");
            return;
        }
        runtime_->Notify(false, "Pixels", "Copied to clipboard");
    }
    void OpenUrl(const std::string& url) override {
        if (url.empty() || !SDL_OpenURL(url.c_str())) {
            runtime_->Notify(true, "Pixels", "Unable to open the complete address");
        }
    }

private:
    struct ActiveSession final {
        std::string id{};
        std::int64_t revision{};
    };

    static std::string CredentialKey(const ParsedConnectionInput& target) {
        if (!target.deviceId.empty()) {
            return "device:" + target.deviceId;
        }
        return target.hosts.empty() ? std::string{} : "endpoint:" + target.hosts.front() + ":" + std::to_string(target.port);
    }

    void ReportImmediateFailure(const std::string& target, const DirectSessionMode mode, const ui::ConnectionFailureReason reason,
                                std::string diagnostic) {
        const auto generation = connectionProgress_.Begin(ConnectionIntentFor(mode), target);
        if (!generation) return;
        connectionProgress_.Fail(*generation, ui::ConnectionStepKind::ValidateTarget, reason, std::move(diagnostic));
    }

    void QueueConnectionInput(std::string target, std::string password, const DirectSessionMode mode) {
        const auto generation = connectionProgress_.Begin(ConnectionIntentFor(mode), target);
        if (!generation) return;
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        const bool queued =
            runtime_->Worker()->Post([weakSelf, generation = *generation, target = std::move(target), password = std::move(password), mode] mutable {
                if (const auto self = weakSelf.lock()) self->RunConnectionInput(generation, std::move(target), std::move(password), mode);
            });
        if (!queued) {
            connectionProgress_.Fail(*generation, ui::ConnectionStepKind::ValidateTarget, ui::ConnectionFailureReason::WorkerUnavailable,
                                     "The Panel background worker is stopping and could not queue the connection preflight.");
        }
    }

    void QueueResolvedConnection(ParsedConnectionInput target, const DirectSessionMode mode) {
        const std::string label{
            !target.deviceId.empty()
                ? target.deviceId
                : (!target.displayName.empty() ? target.displayName : (target.hosts.empty() ? std::string{} : target.hosts.front()))};
        const auto generation = connectionProgress_.Begin(ConnectionIntentFor(mode), label);
        if (!generation) return;
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        const bool queued = runtime_->Worker()->Post([weakSelf, generation = *generation, target = std::move(target), mode] mutable {
            if (const auto self = weakSelf.lock()) {
                self->connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::ValidateTarget);
                self->connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::ResolveDevice);
                if (target.hosts.empty() || target.port <= 0 || target.port > 65535) {
                    self->connectionProgress_.Fail(generation, ui::ConnectionStepKind::ResolveDevice, ui::ConnectionFailureReason::NoUsableAddress,
                                                   "The selected device has no usable host and desktop service port.");
                    return;
                }
                self->connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::ResolveDevice,
                                                      target.hosts.front() + ":" + std::to_string(target.port));
                self->RunDirect(generation, std::move(target), mode);
            }
        });
        if (!queued) {
            connectionProgress_.Fail(*generation, ui::ConnectionStepKind::ValidateTarget, ui::ConnectionFailureReason::WorkerUnavailable,
                                     "The Panel background worker is stopping and could not queue the connection preflight.");
        }
    }

    void RunConnectionInput(const std::uint64_t generation, std::string targetText, std::string password, const DirectSessionMode mode) {
        auto parsed = ParseConnectionInput(std::move(targetText), runtime_->Config()->Ports().desktop);
        if (!parsed) {
            connectionProgress_.Fail(generation, ui::ConnectionStepKind::ValidateTarget, ui::ConnectionFailureReason::InvalidTarget,
                                     "Enter a device ID, a complete link:// address, or IP[:port].");
            return;
        }
        connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::ValidateTarget);
        connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::ResolveDevice);
        if (parsed->kind == ConnectionInputKind::DirectEndpoint) {
            parsed->password = std::move(password);
        }
        if (parsed->kind == ConnectionInputKind::DeviceId) {
            if (!runtime_->Console()->Account().loggedIn) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::ResolveDevice, ui::ConnectionFailureReason::ConsoleLoginRequired,
                                         "Device ID lookup requires a signed-in Console account. A complete link:// address or IP[:port] can be used "
                                         "without lookup.");
                return;
            }
            const auto connection = runtime_->Console()->QueryNativeDeviceConnection(parsed->deviceId, mode == DirectSessionMode::ViewOnly);
            if (!connection) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::ResolveDevice, ui::ConnectionFailureReason::DeviceResolutionFailed,
                                         "Console did not return a native address. The device may be offline or its presence may be stale.");
                return;
            }
            parsed->kind = ConnectionInputKind::DirectEndpoint;
            parsed->password.clear();
            parsed->hosts = {connection->host};
            parsed->port = connection->port;
            parsed->frontendSessionId = connection->session_id;
            parsed->frontendSessionRevision = connection->session_revision;
            parsed->frontendToken = connection->frontend_token;
            parsed->relayHost = connection->relay_host;
            parsed->relayPort = connection->relay_port;
            parsed->relayDeviceId = "server_" + connection->device_id;
            parsed->relayAdmissionTicket = connection->relay_admission_ticket;
            {
                const std::scoped_lock lock{mutex_};
                if (const auto existing = std::ranges::find(devices_, parsed->deviceId, &ui::RemoteDeviceCard::deviceId);
                    existing != devices_.end()) {
                    parsed->displayName = existing->name;
                    parsed->platform = existing->platform;
                }
            }
        }
        if (parsed->hosts.empty() || parsed->port <= 0 || parsed->port > 65535) {
            connectionProgress_.Fail(generation, ui::ConnectionStepKind::ResolveDevice, ui::ConnectionFailureReason::NoUsableAddress,
                                     "No valid remote host and desktop service port were resolved for this connection.");
            return;
        }
        connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::ResolveDevice,
                                        parsed->hosts.front() + ":" + std::to_string(parsed->port));
        RunDirect(generation, std::move(*parsed), mode);
    }

    void RunDirect(const std::uint64_t generation, ParsedConnectionInput target, const DirectSessionMode mode) {
        const bool fileTransfer{mode == DirectSessionMode::FileTransfer};
        const bool viewOnly{mode != DirectSessionMode::Control};
        const std::string credentialKey{CredentialKey(target)};
        if (target.hosts.empty() || target.port <= 0 || target.port > 65535) {
            connectionProgress_.Fail(generation, ui::ConnectionStepKind::ResolveDevice, ui::ConnectionFailureReason::NoUsableAddress,
                                     "No valid remote host and desktop service port were resolved for this connection.");
            return;
        }
        connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::ReachEndpoint);
        std::string endpointFailures{};
        bool renderEndpointReached{};
        bool passwordVerificationUnavailable{};
        const std::string nonce{GetUUID()};
        const bool consoleAuthorized = target.frontendToken && !target.frontendToken->Bytes().empty();
        for (const auto& host : target.hosts) {
            const std::string endpoint{host + ":" + std::to_string(target.port)};
            const auto configuration = RenderApi::GetRenderConfiguration(host, target.port);
            if (!configuration) {
                if (!endpointFailures.empty()) endpointFailures += "; ";
                endpointFailures += endpoint + " returned HTTP/status " + std::to_string(configuration.error());
                continue;
            }
            if (!target.deviceId.empty() && !configuration->device_id_.empty() && configuration->device_id_ != target.deviceId) {
                if (!endpointFailures.empty()) endpointFailures += "; ";
                endpointFailures += endpoint + " belongs to device " + configuration->device_id_ + ", expected " + target.deviceId;
                continue;
            }
            renderEndpointReached = true;
            connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::ReachEndpoint, endpoint);
            connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::CheckPermission);
            if (!configuration->access_policy_known_ || (!fileTransfer && !configuration->controller_availability_known_)) {
                connectionProgress_.Fail(
                    generation, ui::ConnectionStepKind::CheckPermission, ui::ConnectionFailureReason::RemotePreflightUnavailable,
                    "The remote Render does not expose the required access-policy and controller-seat state. No client process was started.");
                return;
            }
            if (!configuration->incoming_remote_access_enabled_) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::CheckPermission, ui::ConnectionFailureReason::RemoteAccessDisabled,
                                         "The remote device reported that incoming desktop control is disabled. No client process was started.");
                return;
            }
            if (fileTransfer && !configuration->file_transfer_enabled_) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::CheckPermission, ui::ConnectionFailureReason::FileTransferDisabled,
                                         "The remote device reported that file transfer is disabled. No file-transfer process was started.");
                return;
            }
            if (!fileTransfer && !configuration->controller_available_) {
                const std::string diagnostic{configuration->controller_reconnect_grace_
                                                 ? "The previous controller is within its reconnect grace period. Retry after " +
                                                       std::to_string(configuration->controller_retry_after_ms_) +
                                                       " ms. No client process was started."
                                                 : "Another controller currently owns the remote desktop. No client process was started."};
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::CheckPermission,
                                         configuration->controller_reconnect_grace_ ? ui::ConnectionFailureReason::RemoteReconnectGrace
                                                                                    : ui::ConnectionFailureReason::RemoteSessionOccupied,
                                         diagnostic);
                return;
            }
            connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::CheckPermission);
            connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::VerifyPassword);
            if (!consoleAuthorized && target.password.empty()) target.password = credentialVault_->Read(credentialKey).value_or(std::string{});
            if (!consoleAuthorized && target.password.empty()) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::VerifyPassword, ui::ConnectionFailureReason::PasswordRequired,
                                         "No saved credential is available. Enter the current password shown on the remote device and retry.");
                return;
            }
            const std::string passwordHash{consoleAuthorized ? std::string{} : MD5::Hex(target.password)};
            if (!consoleAuthorized) {
                const auto verified = RenderApi::VerifySecurityPassword(host, target.port, passwordHash);
                if (!verified) {
                    passwordVerificationUnavailable = true;
                    if (!endpointFailures.empty()) endpointFailures += "; ";
                    endpointFailures += endpoint + " password verification returned HTTP/status " + std::to_string(verified.error());
                    continue;
                }
                if (!verified.value()) {
                    credentialVault_->Delete(credentialKey);
                    connectionProgress_.Fail(generation, ui::ConnectionStepKind::VerifyPassword, ui::ConnectionFailureReason::PasswordRejected,
                                             "The remote device rejected the supplied password. Its temporary password may have changed.");
                    return;
                }
            }
            connectionProgress_.SucceedStep(generation, ui::ConnectionStepKind::VerifyPassword);
            connectionProgress_.BeginStep(generation, ui::ConnectionStepKind::LaunchClient);
            const std::string remoteDeviceId{target.deviceId.empty() ? configuration->device_id_ : target.deviceId};
            const std::string displayName{target.displayName.empty() ? host : target.displayName};
            const std::string sessionId{consoleAuthorized ? target.frontendSessionId : (fileTransfer ? "file-" : "direct-") + GetUUID()};
            const auto preference = runtime_->Config()->LoadRemoteDevicePreference(remoteDeviceId).value_or(RemoteDevicePreference{});
            if (consoleAuthorized && preference.forceRelay &&
                (target.relayHost.empty() || target.relayPort <= 0 || target.relayAdmissionTicket.empty())) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::LaunchClient, ui::ConnectionFailureReason::ClientLaunchFailed,
                                         "Console did not issue a Relay route for this resource session.");
                return;
            }
            const bool launched = runtime_->Launcher()->Launch(
                {.connectionKind =
                     target.kind == ConnectionInputKind::SharedLink ? NativeConnectionKind::SharedLinkDirect : NativeConnectionKind::IpDirect,
                 .displayName = displayName,
                 .remoteDeviceId = remoteDeviceId,
                 .remotePlatform = target.platform,
                 .nonce = nonce,
                 .directHost = host,
                 .directPort = target.port,
                 .directStreamId = sessionId,
                 .remotePasswordHash = passwordHash,
                 .frontendSessionId = target.frontendSessionId,
                 .frontendSessionRevision = target.frontendSessionRevision,
                 .frontendToken = target.frontendToken,
                 .relayHost = target.relayHost,
                 .relayPort = target.relayPort,
                 .relayRemoteDeviceId = target.relayDeviceId.empty() ? "server_" + remoteDeviceId : target.relayDeviceId,
                 .relayAdmissionTicket = target.relayAdmissionTicket,
                 .viewOnly = viewOnly,
                 .forceTcp = preference.forceTcp,
                 .forceRelay = preference.forceRelay,
                 .fileTransfer = fileTransfer,
                 .audio = fileTransfer ? false : preference.audio,
                 .clipboard = fileTransfer ? false : preference.clipboard,
                 .splitWindows = preference.splitWindows,
                 .forceSoftware = preference.forceSoftware,
                 .waitForDebugger = preference.waitForDebugger,
                 .forceGdiCapture = preference.forceGdiCapture,
                 .disableVulkan = preference.disableVulkan});
            if (!launched) {
                connectionProgress_.Fail(generation, ui::ConnectionStepKind::LaunchClient, ui::ConnectionFailureReason::ClientLaunchFailed,
                                         "All preflight checks passed, but px_client could not start. Confirm that px_client.exe is installed beside "
                                         "px_panel.exe and is not blocked by Windows.");
                return;
            }
            connectionProgress_.Complete(generation, fileTransfer ? "Pixels File Transfer started." : "Pixels Client started.");
            if (!consoleAuthorized) {
                static_cast<void>(credentialVault_->Write("device:" + remoteDeviceId, target.password));
                if (credentialKey != "device:" + remoteDeviceId) static_cast<void>(credentialVault_->Write(credentialKey, target.password));
            }
            const auto connectedAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            static_cast<void>(runtime_->Config()->UnhideRemoteDevice(remoteDeviceId));
            static_cast<void>(runtime_->Config()->SaveRemoteDeviceHistory(
                {.deviceId = remoteDeviceId, .name = displayName, .host = host, .port = target.port, .lastConnectedAt = connectedAt}));
            const std::scoped_lock lock{mutex_};
            std::string cardId{"direct-" + remoteDeviceId + "-" + host + ":" + std::to_string(target.port)};
            if (const auto consoleCard = std::ranges::find(devices_, remoteDeviceId, &ui::RemoteDeviceCard::deviceId);
                consoleCard != devices_.end() && consoleCard->streamId.starts_with("console-device-")) {
                cardId = consoleCard->streamId;
            }
            const ui::RemoteDeviceCard card{.streamId = cardId,
                                            .name = displayName,
                                            .deviceId = remoteDeviceId,
                                            .online = true,
                                            .host = host,
                                            .port = target.port,
                                            .lastConnectedAt = connectedAt,
                                            .audio = true,
                                            .clipboard = true,
                                            .viewOnly = viewOnly};
            if (const auto existing = std::ranges::find(devices_, cardId, &ui::RemoteDeviceCard::streamId); existing != devices_.end()) {
                *existing = card;
            } else {
                devices_.push_back(card);
            }
            if (!fileTransfer) {
                activeSessions_[cardId] = ActiveSession{.id = sessionId, .revision = target.frontendSessionRevision};
            }
            return;
        }
        if (renderEndpointReached && passwordVerificationUnavailable) {
            connectionProgress_.Fail(generation, ui::ConnectionStepKind::VerifyPassword, ui::ConnectionFailureReason::PasswordVerificationUnavailable,
                                     std::move(endpointFailures));
            return;
        }
        connectionProgress_.Fail(
            generation, ui::ConnectionStepKind::ReachEndpoint, ui::ConnectionFailureReason::DeviceUnreachable,
            endpointFailures.empty() ? "No advertised remote address responded to the Render configuration request." : std::move(endpointFailures));
    }

    void RefreshDevices() {
        const auto runtime = runtime_;
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf] {
            const auto endpoint = runtime->Config()->Console();
            bool managerOnline{};
            if (endpoint) {
                const auto ping = px_console::QueryConsoleReady(endpoint->host, endpoint->port);
                managerOnline = ping && ping.value();
            }
            auto devices = runtime->Console()->QueryDevices();
            const auto self = weakSelf.lock();
            if (!self) return;
            self->managerOnline_.store(managerOnline, std::memory_order_release);
            std::vector<ui::RemoteDeviceCard> previous{};
            {
                const std::scoped_lock lock{self->mutex_};
                previous = self->devices_;
            }
            std::vector<ui::RemoteDeviceCard> cards{};
            std::unordered_set<std::string> consoleDeviceIds{};
            const auto history = runtime->Config()->LoadRemoteDeviceHistory();
            for (const auto& binding : devices) {
                if (!binding || binding->device_id_.empty() || !consoleDeviceIds.insert(binding->device_id_).second) continue;
                if (runtime->Config()->RemoteDeviceHidden(binding->device_id_)) continue;
                ui::RemoteDeviceCard card{.streamId = "console-device-" + binding->device_id_,
                                          .name = binding->device_name_,
                                          .deviceId = binding->device_id_,
                                          .platform = px::ui::ParseDevicePlatform(binding->platform_),
                                          .online = !binding->disabled_,
                                          .audio = true,
                                          .clipboard = true};
                if (const auto connected = std::ranges::find(history, binding->device_id_, &RemoteDeviceHistory::deviceId);
                    connected != history.end()) {
                    card.lastConnectedAt = connected->lastConnectedAt;
                    card.host = connected->host;
                    card.port = connected->port;
                }
                if (const auto saved = runtime->Config()->LoadRemoteDevicePreference(binding->device_id_)) {
                    card.name = saved->name.empty() ? card.name : saved->name;
                    card.audio = saved->audio;
                    card.clipboard = saved->clipboard;
                    card.viewOnly = saved->viewOnly;
                    card.splitWindows = saved->splitWindows;
                    card.forceSoftware = saved->forceSoftware;
                    card.forceTcp = saved->forceTcp;
                    card.forceRelay = saved->forceRelay;
                    card.waitForDebugger = saved->waitForDebugger;
                    card.forceGdiCapture = saved->forceGdiCapture;
                    card.disableVulkan = saved->disableVulkan;
                }
                cards.push_back(std::move(card));
            }
            std::unordered_set<std::string> retainedDirectDeviceIds{};
            for (const auto& historyItem : history) {
                if (consoleDeviceIds.contains(historyItem.deviceId) || runtime->Config()->RemoteDeviceHidden(historyItem.deviceId)) continue;
                const auto saved = runtime->Config()->LoadRemoteDevicePreference(historyItem.deviceId).value_or(RemoteDevicePreference{});
                cards.push_back({.streamId = "direct-" + historyItem.deviceId + "-" + historyItem.host + ":" + std::to_string(historyItem.port),
                                 .name = saved.name.empty() ? historyItem.name : saved.name,
                                 .deviceId = historyItem.deviceId,
                                 .online = false,
                                 .host = historyItem.host,
                                 .port = historyItem.port,
                                 .lastConnectedAt = historyItem.lastConnectedAt,
                                 .audio = saved.audio,
                                 .clipboard = saved.clipboard,
                                 .viewOnly = saved.viewOnly,
                                 .splitWindows = saved.splitWindows,
                                 .forceSoftware = saved.forceSoftware,
                                 .forceTcp = saved.forceTcp,
                                 .forceRelay = saved.forceRelay,
                                 .waitForDebugger = saved.waitForDebugger,
                                 .forceGdiCapture = saved.forceGdiCapture,
                                 .disableVulkan = saved.disableVulkan});
                retainedDirectDeviceIds.insert(historyItem.deviceId);
            }
            for (const auto& existing : previous) {
                if (!existing.streamId.starts_with("direct-") || existing.host.empty() || consoleDeviceIds.contains(existing.deviceId) ||
                    runtime->Config()->RemoteDeviceHidden(existing.deviceId) ||
                    (!existing.deviceId.empty() && !retainedDirectDeviceIds.insert(existing.deviceId).second)) {
                    continue;
                }
                cards.push_back(existing);
            }
            std::ranges::sort(cards, [](const ui::RemoteDeviceCard& left, const ui::RemoteDeviceCard& right) {
                if (left.lastConnectedAt != right.lastConnectedAt) return left.lastConnectedAt > right.lastConnectedAt;
                if (left.online != right.online) return left.online;
                return left.name < right.name;
            });
            const std::scoped_lock lock{self->mutex_};
            self->devices_ = std::move(cards);
        }));
    }

    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::shared_ptr<PanelCredentialVault> credentialVault_{};
    mutable std::mutex mutex_{};
    std::vector<ui::RemoteDeviceCard> devices_{};
    std::unordered_map<std::string, ActiveSession> activeSessions_{};
    ConnectionProgressTracker connectionProgress_{};
    std::atomic_bool showPassword_{};
    std::atomic_bool managerOnline_{};
    std::shared_ptr<RefreshLoopState> refreshLoopState_{std::make_shared<RefreshLoopState>()};
    std::jthread refreshThread_{};
};

}  // namespace

std::shared_ptr<ui::RemoteControlPort> CreateProductRemoteControlPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    auto result = std::make_shared<ProductRemoteControlPort>(runtime);
    result->Initialize();
    return result;
}

}  // namespace px::panel::product
