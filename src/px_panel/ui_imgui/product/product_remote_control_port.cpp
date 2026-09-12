#include "panel_connection_input.h"
#include "panel_connection_links.h"
#include "panel_credential_vault.h"
#include "panel_product_runtime.h"

#include "px_common/ip_util.h"
#include "px_common/md5.h"
#include "px_common/uuid.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"
#include "px_console_client/console_user_device.h"
#include "render_panel/network/render_api.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace px::panel::product {
namespace {

class ProductRemoteControlPort final : public ui::RemoteControlPort, public std::enable_shared_from_this<ProductRemoteControlPort> {
  public:
    explicit ProductRemoteControlPort(std::shared_ptr<PanelProductRuntime> runtime)
        : runtime_{std::move(runtime)}, credentialVault_{PanelCredentialVault::Create()} {}
    void Initialize() {
        showPassword_.store(runtime_->Config()->ShowTemporaryPassword(), std::memory_order_release);
        RefreshDevices();
    }

    ui::RemoteControlState Snapshot() const override {
        const auto identity = runtime_->Config()->Identity();
        const auto endpoint = runtime_->Config()->Console();
        const auto ports = runtime_->Config()->Ports();
        std::vector<std::string> localAddresses{};
        for (const auto& adapter : IPUtil::ScanIPs()) {
            if (!adapter.ip_addr_.empty()) {
                localAddresses.push_back(adapter.ip_addr_);
            }
        }
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
    void Refresh() override {
        RefreshDevices();
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
            StartDirect(std::move(direct), viewOnly);
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
        StartDirect(std::move(direct), viewOnly);
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
                            viewOnly);
                return;
            }
        }
        if (!deviceId.empty()) {
            Connect(std::move(deviceId), {}, viewOnly);
            return;
        }
        runtime_->Notify(true, "Connection failed", "This device has no usable native address. Refresh the device list after it comes online.");
    }
    void StartFileTransfer(const std::string& streamId) override {
        std::string sessionId{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = activeSessions_.find(streamId); found != activeSessions_.end())
                sessionId = found->second;
        }
        if (!sessionId.empty() && runtime_->LocalServer()->OpenFileTransfer(sessionId))
            return;
        {
            const std::scoped_lock lock{mutex_};
            const auto direct = std::ranges::find(devices_, streamId, &ui::RemoteDeviceCard::streamId);
            if (direct != devices_.end() && !direct->host.empty()) {
                runtime_->Notify(true, "File transfer", "Start the direct control session before opening file transfer");
                return;
            }
        }
        if (sessionId.empty())
            runtime_->Notify(true, "File transfer", "Start a control session before opening file transfer");
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

    void SendDeviceCommand(const std::string&, const ui::RemoteDeviceCommand) override {
        runtime_->Notify(true, "Pixels", "Device commands require an active control session");
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
        static_cast<void>(runtime_->Config()->DeleteRemoteDevicePreference(deviceId));
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
        const std::scoped_lock lock{mutex_};
        if (const auto found = std::ranges::find(devices_, device.streamId, &ui::RemoteDeviceCard::streamId); found != devices_.end())
            *found = std::move(device);
    }

    void CopyText(const std::string& text) override {
        if (text.empty() || !SDL_SetClipboardText(text.c_str())) {
            runtime_->Notify(true, "Pixels", "Unable to copy the complete link");
            return;
        }
        runtime_->Notify(false, "Pixels", "Complete link copied");
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

    void StartDirect(ParsedConnectionInput target, const bool viewOnly = false) {
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
        static_cast<void>(runtime_->Worker()->Post([runtime, credentialVault, weakSelf, target = std::move(target), credentialKey, viewOnly] {
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
                const std::string sessionId{"direct-" + GetUUID()};
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
                     .audio = preference.audio,
                     .clipboard = preference.clipboard,
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
                const std::string cardId{"direct-" + remoteDeviceId + "-" + host + ":" + std::to_string(target.port)};
                const ui::RemoteDeviceCard card{.streamId = cardId,
                                                .name = displayName,
                                                .deviceId = remoteDeviceId,
                                                .online = true,
                                                .host = host,
                                                .port = target.port,
                                                .audio = true,
                                                .clipboard = true,
                                                .viewOnly = viewOnly};
                const std::scoped_lock lock{self->mutex_};
                if (const auto existing = std::ranges::find(self->devices_, cardId, &ui::RemoteDeviceCard::streamId);
                    existing != self->devices_.end()) {
                    *existing = card;
                } else {
                    self->devices_.push_back(card);
                }
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
            std::vector<ui::RemoteDeviceCard> cards{};
            for (const auto& binding : devices) {
                if (!binding || !binding->device_)
                    continue;
                ui::RemoteDeviceCard card{.streamId = "console-device-" + binding->device_id_,
                                          .name = binding->device_->device_name_,
                                          .deviceId = binding->device_id_,
                                          .online = binding->device_->active_,
                                          .audio = true,
                                          .clipboard = true};
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
                {
                    const std::scoped_lock lock{self->mutex_};
                    if (const auto saved = std::ranges::find(self->devices_, card.streamId, &ui::RemoteDeviceCard::streamId);
                        saved != self->devices_.end()) {
                        const bool online{card.online};
                        const std::string authoritativeName{card.name};
                        card = *saved;
                        card.online = online;
                        if (card.name.empty())
                            card.name = authoritativeName;
                    }
                }
                cards.push_back(std::move(card));
            }
            const std::scoped_lock lock{self->mutex_};
            for (const auto& existing : self->devices_) {
                if (!existing.host.empty()) {
                    cards.push_back(existing);
                }
            }
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
};

} // namespace

std::shared_ptr<ui::RemoteControlPort> CreateProductRemoteControlPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    auto result = std::make_shared<ProductRemoteControlPort>(runtime);
    result->Initialize();
    return result;
}

} // namespace px::panel::product
