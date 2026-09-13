#include "panel_connection_input.h"
#include "panel_connection_links.h"
#include "panel_credential_vault.h"
#include "panel_device_registration.h"
#include "panel_product_runtime.h"

#include "px_common/http_client.h"
#include "px_common/md5.h"
#include "px_common/uuid.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_user_device.h"
#include "px_relay_client/relay_api.h"
#include "render_api.h"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace px::panel::product {
namespace {

enum class DirectSessionMode : std::uint8_t {
    Control,
    ViewOnly,
    FileTransfer,
};

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
    if (host.empty() || port <= 0 || port > 65535)
        return false;
    const auto client = HttpClient::Make(host, port, "/panel/stream/message", 3000);
    if (!client)
        return false;
    const auto response = client->Post({}, payload, "application/json");
    if (response.status != 200 || response.body.empty())
        return false;
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
        if (refreshThread_.joinable())
            refreshThread_.join();
    }
    void Initialize() {
        showPassword_.store(runtime_->Config()->ShowTemporaryPassword(), std::memory_order_release);
        const auto runtime = runtime_;
        static_cast<void>(runtime_->Worker()->Post([runtime] { static_cast<void>(EnsurePanelDeviceRegistration(runtime)); }));
        RefreshDevices();
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        const auto loopState = refreshLoopState_;
        refreshThread_ = std::jthread{[weakSelf, loopState](const std::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                {
                    std::unique_lock lock{loopState->mutex};
                    loopState->wakeup.wait_for(lock, stopToken, std::chrono::seconds{2}, [] { return false; });
                }
                if (stopToken.stop_requested())
                    return;
                const auto self = weakSelf.lock();
                if (!self)
                    return;
                self->RefreshDevices();
            }
        }};
    }

    ui::RemoteControlState Snapshot() const override {
        const auto identity = runtime_->Config()->Identity();
        const auto endpoint = runtime_->Config()->Console();
        const auto ports = runtime_->Config()->Ports();
        const auto localAddresses = CollectPanelLocalAddresses();
        const auto links = BuildPanelConnectionLinks(identity, ports, endpoint, runtime_->Config()->NodePublicAddress(), localAddresses);
        ui::RemoteControlState result{.deviceId = identity.deviceId,
                                      .temporaryPassword = identity.randomPassword,
                                      .deviceName = identity.deviceName,
                                      .desktopLink = links.desktop,
                                      .webClientAddress = links.web,
                                      .showTemporaryPassword = showPassword_,
                                      .managerOnline = managerOnline_.load(std::memory_order_acquire)};
        {
            const std::scoped_lock lock{mutex_};
            result.devices = devices_;
        }
        return result;
    }

    void SetPasswordVisible(const bool visible) override {
        showPassword_ = visible;
        static_cast<void>(runtime_->Config()->SaveShowTemporaryPassword(visible));
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
            const auto endpoint = runtime->Config()->Console();
            const auto identity = runtime->Config()->Identity();
            if (!endpoint || identity.deviceId.empty()) {
                runtime->Notify(true, "Device name", "The management service is not configured. The device name was not changed.");
                return;
            }
            const auto updated = px_console::ConsoleDeviceApi::UpdateDeviceName(endpoint->host, endpoint->port, endpoint->appKey, identity.deviceId,
                                                                                deviceName, MD5::Hex(identity.randomPassword));
            if (!updated || !updated.value()) {
                runtime->Notify(true, "Device name", "The management service rejected the new device name. Nothing was changed locally.");
                return;
            }
            if (!runtime->Config()->SaveCustomDeviceName(deviceName)) {
                static_cast<void>(px_console::ConsoleDeviceApi::UpdateDeviceName(endpoint->host, endpoint->port, endpoint->appKey, identity.deviceId,
                                                                                 identity.deviceName, MD5::Hex(identity.randomPassword)));
                runtime->Notify(true, "Device name", "The local device name could not be saved; the management change was rolled back.");
                return;
            }
            static_cast<void>(runtime->Service()->RestartRender());
            runtime->Notify(false, "Device name", "Device name updated locally and on the management service.");
        }));
    }
    void Refresh() override {
        RefreshDevices();
    }
    void RefreshTemporaryPassword() override {
        const auto runtime = runtime_;
        static_cast<void>(runtime_->Worker()->Post([runtime] {
            const auto endpoint = runtime->Config()->Console();
            auto identity = runtime->Config()->Identity();
            if (!endpoint || identity.deviceId.empty()) {
                runtime->Notify(true, "Password", "The management service is not configured. The temporary password was not changed.");
                return;
            }
            const auto updated = px_console::ConsoleDeviceApi::UpdateRandomPwd(endpoint->host, endpoint->port, endpoint->appKey, identity.deviceId);
            if (!updated || !updated.value() || updated.value()->gen_random_pwd_.empty()) {
                runtime->Notify(true, "Password", "The management service could not refresh the temporary password.");
                return;
            }
            identity.randomPassword = updated.value()->gen_random_pwd_;
            if (!runtime->Config()->SaveIdentity(identity)) {
                runtime->Notify(true, "Password", "The new temporary password could not be saved locally.");
                return;
            }
            if (!runtime->Service()->RestartRender()) {
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
        return !credentialVault_->Read(CredentialKey(*parsed)).has_value();
    }

    void Connect(std::string target, std::string password, const bool viewOnly) override {
        const auto parsed = ParseConnectionInput(std::move(target), runtime_->Config()->Ports().desktop);
        if (!parsed) {
            runtime_->Notify(true, "Connection failed",
                             "The connection target is incomplete or malformed. Enter a device ID, a complete link:// address, or IP[:port].");
            return;
        }
        if (parsed->kind != ConnectionInputKind::DeviceId) {
            auto direct = *parsed;
            if (direct.kind == ConnectionInputKind::DirectEndpoint) {
                direct.password = std::move(password);
            }
            StartDirect(std::move(direct), viewOnly ? DirectSessionMode::ViewOnly : DirectSessionMode::Control);
            return;
        }
        if (!runtime_->Console()->Account().loggedIn) {
            runtime_->Notify(true, "Connection failed",
                             "Device ID lookup requires a Console account. Sign in first, or connect with a complete link:// address or IP[:port].");
            return;
        }
        ParsedConnectionInput direct{.kind = ConnectionInputKind::DirectEndpoint, .deviceId = parsed->deviceId, .password = std::move(password)};
        const auto connection = runtime_->Console()->QueryNativeDeviceConnection(parsed->deviceId);
        if (!connection) {
            runtime_->Notify(
                true, "Connection failed",
                "Console could not resolve a native address for this device. Confirm that the device is online, then refresh and retry.");
            return;
        }
        direct.hosts = {connection->host};
        direct.port = connection->port;
        direct.relayHost = connection->relay_host;
        direct.relayPort = connection->relay_port;
        direct.relayDeviceId = connection->signal_device_id;
        {
            const std::scoped_lock lock{mutex_};
            const auto existing = std::ranges::find(devices_, parsed->deviceId, &ui::RemoteDeviceCard::deviceId);
            if (existing != devices_.end())
                direct.displayName = existing->name;
        }
        StartDirect(std::move(direct), viewOnly ? DirectSessionMode::ViewOnly : DirectSessionMode::Control);
    }

    void StartStream(const std::string& streamId, const bool viewOnly) override {
        std::string deviceId{};
        {
            const std::scoped_lock lock{mutex_};
            const auto direct = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId);
            if (direct != devices_.end() && direct->host.empty()) {
                deviceId = direct->deviceId;
            } else if (direct != devices_.end()) {
                StartDirect({.kind = ConnectionInputKind::DirectEndpoint,
                             .deviceId = direct->deviceId,
                             .displayName = direct->name,
                             .hosts = {direct->host},
                             .port = direct->port,
                             .password = {}},
                            viewOnly ? DirectSessionMode::ViewOnly : DirectSessionMode::Control);
                return;
            }
        }
        if (!deviceId.empty()) {
            Connect(std::move(deviceId), {}, viewOnly);
            return;
        }
        runtime_->Notify(true, "Connection failed", "This device has no usable native address. Refresh the device list after it comes online.");
    }
    void StartFileTransfer(const std::string& streamId, std::string password) override {
        ui::RemoteDeviceCard target{};
        {
            const std::scoped_lock lock{mutex_};
            const auto found = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId);
            if (found == devices_.end()) {
                runtime_->Notify(true, "File transfer", "The selected device is no longer in the device list. Refresh and retry.");
                return;
            }
            target = *found;
        }

        ParsedConnectionInput direct{.kind = ConnectionInputKind::DirectEndpoint,
                                     .deviceId = target.deviceId,
                                     .displayName = target.name,
                                     .hosts = target.host.empty() ? std::vector<std::string>{} : std::vector<std::string>{target.host},
                                     .port = target.port,
                                     .password = std::move(password)};
        if (direct.hosts.empty() && !target.deviceId.empty()) {
            const auto connection = runtime_->Console()->QueryNativeDeviceConnection(target.deviceId);
            if (!connection) {
                runtime_->Notify(true, "File transfer",
                                 "Console could not resolve a native address for this device. Confirm that it is online, then refresh and retry.");
                return;
            }
            direct.hosts = {connection->host};
            direct.port = connection->port;
            direct.relayHost = connection->relay_host;
            direct.relayPort = connection->relay_port;
            direct.relayDeviceId = connection->signal_device_id;
        }
        if (direct.hosts.empty() || direct.port <= 0) {
            runtime_->Notify(true, "File transfer", "This device has no usable native address. Refresh the device list and retry.");
            return;
        }
        StartDirect(std::move(direct), DirectSessionMode::FileTransfer);
    }

    void StopStream(const std::string& streamId) override {
        std::string sessionId{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = activeSessions_.find(streamId); found != activeSessions_.end()) {
                sessionId = std::move(found->second);
                activeSessions_.erase(found);
            }
        }
        if (!sessionId.empty())
            static_cast<void>(runtime_->Launcher()->Stop(sessionId));
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
            const auto endpoint = runtime->Config()->Console();
            const auto identity = runtime->Config()->Identity();
            auto host = target.host;
            int port{target.port};
            std::string relayHost{endpoint ? endpoint->host : std::string{}};
            int relayPort{endpoint ? endpoint->relayPort : 0};
            std::string relayDeviceId{target.deviceId.empty() ? std::string{} : "server_" + target.deviceId};
            if (!target.deviceId.empty()) {
                if (const auto connection = runtime->Console()->QueryNativeDeviceConnection(target.deviceId)) {
                    if (!connection->host.empty())
                        host = connection->host;
                    if (connection->port > 0)
                        port = connection->port;
                    if (!connection->relay_host.empty())
                        relayHost = connection->relay_host;
                    if (connection->relay_port > 0)
                        relayPort = connection->relay_port;
                    if (!connection->signal_device_id.empty())
                        relayDeviceId = connection->signal_device_id;
                }
            }

            const std::string payload{nlohmann::json{{"event", DeviceCommandEvent(command)}, {"from_device", identity.deviceId}}.dump()};
            bool delivered{SendDirectDeviceCommand(host, port, payload)};
            if (!delivered && endpoint && !relayHost.empty() && relayPort > 0 && !relayDeviceId.empty()) {
                const auto result =
                    px_relay::RelayApi::NotifyEvent(relayHost, relayPort, identity.deviceId, relayDeviceId, payload, endpoint->appKey);
                delivered = result && result.value() == px_relay::kRelayOk;
            }
            runtime->Notify(!delivered, std::string{DeviceCommandName(command)},
                            delivered ? "The command was sent to the device."
                                      : "The command could not reach the device through either its direct or relay connection.");
        });
        if (!queued)
            runtime_->Notify(true, std::string{DeviceCommandName(command)}, "The command could not be queued. Please retry.");
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
    static std::string CredentialKey(const ParsedConnectionInput& target) {
        if (!target.deviceId.empty()) {
            return "device:" + target.deviceId;
        }
        return target.hosts.empty() ? std::string{} : "endpoint:" + target.hosts.front() + ":" + std::to_string(target.port);
    }

    void StartDirect(ParsedConnectionInput target, const DirectSessionMode mode = DirectSessionMode::Control) {
        const bool fileTransfer{mode == DirectSessionMode::FileTransfer};
        const bool viewOnly{mode != DirectSessionMode::Control};
        const std::string credentialKey{CredentialKey(target)};
        if (target.password.empty()) {
            target.password = credentialVault_->Read(credentialKey).value_or(std::string{});
        }
        if (target.password.empty()) {
            runtime_->Notify(true, "Connection failed", "Enter the current password shown on the remote device before connecting.");
            return;
        }
        const auto runtime = runtime_;
        const auto credentialVault = credentialVault_;
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, credentialVault, weakSelf, target = std::move(target), credentialKey, viewOnly,
                                                    fileTransfer] {
            const std::string nonce{GetUUID()};
            const std::string passwordHash{MD5::Hex(target.password)};
            bool renderEndpointReached{};
            for (const auto& host : target.hosts) {
                const auto configuration = RenderApi::GetRenderConfiguration(host, target.port);
                if (!configuration)
                    continue;
                renderEndpointReached = true;
                const auto verified = RenderApi::VerifySecurityPassword(host, target.port, passwordHash);
                if (!verified)
                    continue;
                if (!verified.value()) {
                    credentialVault->Delete(credentialKey);
                    runtime->Notify(true, "Connection failed",
                                    "The device rejected this password. Enter the current password shown on the remote device and retry.");
                    return;
                }
                const std::string remoteDeviceId{target.deviceId.empty() ? configuration.value().device_id_ : target.deviceId};
                const std::string displayName{target.displayName.empty() ? host : target.displayName};
                const std::string sessionId{(fileTransfer ? "file-" : "direct-") + GetUUID()};
                const auto preference = runtime->Config()->LoadRemoteDevicePreference(remoteDeviceId).value_or(RemoteDevicePreference{});
                const auto console = runtime->Config()->Console();
                const bool launched = runtime->Launcher()->Launch(
                    {.connectionKind =
                         target.kind == ConnectionInputKind::SharedLink ? NativeConnectionKind::SharedLinkDirect : NativeConnectionKind::IpDirect,
                     .displayName = displayName,
                     .remoteDeviceId = remoteDeviceId,
                     .nonce = nonce,
                     .directHost = host,
                     .directPort = target.port,
                     .directStreamId = sessionId,
                     .remotePasswordHash = passwordHash,
                     .relayHost = target.relayHost.empty() && console ? console->host : target.relayHost,
                     .relayPort = target.relayPort <= 0 && console ? console->relayPort : target.relayPort,
                     .relayRemoteDeviceId = target.relayDeviceId.empty() ? "server_" + remoteDeviceId : target.relayDeviceId,
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
                const auto self = weakSelf.lock();
                if (!self)
                    return;
                if (!launched) {
                    runtime->Notify(true, "Connection failed",
                                    "Password verification succeeded, but px_client could not start. Confirm that px_client.exe is installed beside "
                                    "px_panel.exe.");
                    return;
                }
                static_cast<void>(credentialVault->Write("device:" + remoteDeviceId, target.password));
                if (credentialKey != "device:" + remoteDeviceId) {
                    static_cast<void>(credentialVault->Write(credentialKey, target.password));
                }
                const auto connectedAt =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                static_cast<void>(runtime->Config()->UnhideRemoteDevice(remoteDeviceId));
                static_cast<void>(runtime->Config()->SaveRemoteDeviceHistory(
                    {.deviceId = remoteDeviceId, .name = displayName, .host = host, .port = target.port, .lastConnectedAt = connectedAt}));
                const std::scoped_lock lock{self->mutex_};
                std::string cardId{"direct-" + remoteDeviceId + "-" + host + ":" + std::to_string(target.port)};
                if (const auto consoleCard = std::ranges::find(self->devices_, remoteDeviceId, &ui::RemoteDeviceCard::deviceId);
                    consoleCard != self->devices_.end() && consoleCard->streamId.starts_with("console-device-")) {
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
                if (const auto existing = std::ranges::find(self->devices_, cardId, &ui::RemoteDeviceCard::streamId);
                    existing != self->devices_.end()) {
                    *existing = card;
                } else {
                    self->devices_.push_back(card);
                }
                if (!fileTransfer)
                    self->activeSessions_[cardId] = sessionId;
                return;
            }
            if (weakSelf.lock()) {
                runtime->Notify(
                    true, "Connection failed",
                    renderEndpointReached
                        ? "The device responded, but password verification was unavailable on every advertised address. Check the Render service "
                          "and control port."
                        : "The device could not be reached at any advertised native address. Check its online status, address, port, and firewall.");
            }
        }));
    }

    void RefreshDevices() {
        const auto runtime = runtime_;
        const std::weak_ptr<ProductRemoteControlPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf] {
            const auto endpoint = runtime->Config()->Console();
            bool managerOnline{};
            if (endpoint) {
                const auto ping = px_console::ConsoleDeviceApi::Ping(endpoint->host, endpoint->port, endpoint->appKey);
                managerOnline = ping && ping.value();
            }
            auto devices = runtime->Console()->QueryDevices();
            const auto self = weakSelf.lock();
            if (!self)
                return;
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
                if (!binding || !binding->device_ || binding->device_id_.empty() || !consoleDeviceIds.insert(binding->device_id_).second)
                    continue;
                if (runtime->Config()->RemoteDeviceHidden(binding->device_id_))
                    continue;
                ui::RemoteDeviceCard card{.streamId = "console-device-" + binding->device_id_,
                                          .name = binding->device_->device_name_,
                                          .deviceId = binding->device_id_,
                                          .platform = px::ui::ParseDevicePlatform(binding->device_->platform_),
                                          .online = binding->device_->active_,
                                          .audio = true,
                                          .clipboard = true};
                if (const auto connected = std::ranges::find(history, binding->device_id_, &RemoteDeviceHistory::deviceId);
                    connected != history.end()) {
                    card.lastConnectedAt = connected->lastConnectedAt;
                    card.host = connected->host;
                    card.port = connected->port;
                }
                const auto parsedLink = ParseConnectionInput(binding->device_->desktop_link_, runtime->Config()->Ports().desktop);
                if (parsedLink && !parsedLink->hosts.empty()) {
                    card.host = parsedLink->hosts.front();
                    card.port = parsedLink->port;
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
                if (consoleDeviceIds.contains(historyItem.deviceId) || runtime->Config()->RemoteDeviceHidden(historyItem.deviceId))
                    continue;
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
                if (left.lastConnectedAt != right.lastConnectedAt)
                    return left.lastConnectedAt > right.lastConnectedAt;
                if (left.online != right.online)
                    return left.online;
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
    std::unordered_map<std::string, std::string> activeSessions_{};
    std::atomic_bool showPassword_{};
    std::atomic_bool managerOnline_{};
    std::shared_ptr<RefreshLoopState> refreshLoopState_{std::make_shared<RefreshLoopState>()};
    std::jthread refreshThread_{};
};

} // namespace

std::shared_ptr<ui::RemoteControlPort> CreateProductRemoteControlPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    auto result = std::make_shared<ProductRemoteControlPort>(runtime);
    result->Initialize();
    return result;
}

} // namespace px::panel::product
