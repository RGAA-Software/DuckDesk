#include "panel_product_runtime.h"
#include "panel_credential_vault.h"

#include "px_common/md5.h"
#include "px_common/uuid.h"
#include "px_console_client/console_errors.h"
#include "render_api.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace px::panel::product {
namespace {

class ProductCloudApplicationsPort final : public ui::CloudApplicationsPort, public std::enable_shared_from_this<ProductCloudApplicationsPort> {
  public:
    explicit ProductCloudApplicationsPort(std::shared_ptr<PanelProductRuntime> runtime)
        : runtime_{std::move(runtime)}, credentialVault_{PanelCredentialVault::Create()} {}
    void Initialize() {
        Refresh();
    }

    std::vector<ui::CloudApplicationCard> Snapshot() override {
        const std::scoped_lock lock{mutex_};
        return cards_;
    }

    void Refresh() override {
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf] {
            const auto applications = runtime->Console()->QueryApplications();
            const auto self = weakSelf.lock();
            if (!self)
                return;
            std::vector<ui::CloudApplicationCard> cards{};
            std::unordered_map<std::string, std::string> instances{};
            std::unordered_map<std::string, std::pair<bool, bool>> preferences{};
            {
                const std::scoped_lock lock{self->mutex_};
                preferences = self->preferences_;
            }
            for (const auto& application : applications) {
                const auto inMemory = preferences.find(application.app_id);
                const auto persisted = runtime->Config()->LoadCloudApplicationPreference(application.app_id);
                const bool forceTcp{inMemory != preferences.end() ? inMemory->second.first : persisted.forceTcp};
                const bool forceRelay{inMemory != preferences.end() ? inMemory->second.second : persisted.forceRelay};
                cards.push_back({.streamId = application.app_id,
                                 .name = application.name,
                                 .instanceState = application.running_instance ? application.running_instance->state : "stopped",
                                 .rdpMode = application.access_mode == "rdp",
                                 .forceTcp = forceTcp,
                                 .forceRelay = forceRelay});
                if (application.running_instance)
                    instances[application.app_id] = application.running_instance->instance_id;
            }
            const std::scoped_lock lock{self->mutex_};
            self->cards_ = std::move(cards);
            self->instanceIds_ = std::move(instances);
        }));
    }

    void Start(const std::string& streamId, const bool viewOnly) override {
        ui::CloudApplicationCard card{};
        std::string existingInstance{};
        {
            const std::scoped_lock lock{mutex_};
            const auto found = std::ranges::find(cards_, streamId, &ui::CloudApplicationCard::streamId);
            if (found == cards_.end())
                return;
            card = *found;
            if (const auto instance = instanceIds_.find(streamId); instance != instanceIds_.end())
                existingInstance = instance->second;
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = std::ranges::find(cards_, streamId, &ui::CloudApplicationCard::streamId); found != cards_.end()) {
                found->instanceState = "starting";
            }
        }
        static_cast<void>(
            runtime_->Worker()->Post([runtime, weakSelf, card = std::move(card), existingInstance = std::move(existingInstance), viewOnly] {
                const auto self = weakSelf.lock();
                if (!self)
                    return;
                const std::string nonce{GetUUID()};
                std::string instanceId{existingInstance};
                std::string instanceState{existingInstance.empty() ? std::string{} : card.instanceState};
                if (instanceId.empty()) {
                    const auto instance = runtime->Console()->StartApplication(card.streamId, nonce);
                    if (!instance) {
                        self->SetInstanceState(card.streamId, "stopped");
                        const std::string serverMessage{px_console::ConsoleApiLastErrorMessage()};
                        runtime->Notify(true, "Application failed",
                                        serverMessage.empty() ? px_console::ConsoleApiErrorAsString(instance.error()) : serverMessage);
                        return;
                    }
                    instanceId = instance->instance_id;
                    instanceState = instance->state;
                }
                for (int attempt{}; instanceState != "running" && attempt < 90; ++attempt) {
                    if (instanceState == "failed" || instanceState == "stopped") {
                        self->SetInstanceState(card.streamId, instanceState);
                        runtime->Notify(true, "Application failed",
                                        "The Render node rejected the application start request. Check the application path and node status.");
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{500});
                    const auto applications = runtime->Console()->QueryApplications();
                    for (const auto& application : applications) {
                        if (application.app_id == card.streamId && application.running_instance &&
                            application.running_instance->instance_id == instanceId) {
                            instanceState = application.running_instance->state;
                            break;
                        }
                    }
                }
                if (instanceState != "running") {
                    self->SetInstanceState(card.streamId, "stopped");
                    runtime->Notify(true, "Application failed",
                                    "The application did not become ready within 45 seconds. It may still be starting on the Render node.");
                    return;
                }
                auto connection = runtime->Console()->QueryNativeApplicationConnection(instanceId, viewOnly);
                if (!connection) {
                    self->SetInstanceState(card.streamId, "running");
                    runtime->Notify(true, "Application failed",
                                    "The application is running, but Console did not return its native connection address. Refresh and retry.");
                    return;
                }
                PendingLaunch launch{.card = card,
                                     .connection = std::move(*connection),
                                     .instanceId = instanceId,
                                     .nonce = nonce,
                                     .sessionId = "app-" + GetUUID(),
                                     .viewOnly = viewOnly};
                const auto password = self->credentialVault_->Read("device:" + launch.connection.device_id);
                if (password) {
                    self->Launch(std::move(launch), *password);
                } else {
                    const std::scoped_lock lock{self->mutex_};
                    self->pendingLaunch_ = std::move(launch);
                }
            }));
    }

    std::optional<ui::CloudApplicationPasswordRequest> PendingPasswordRequest() const override {
        const std::scoped_lock lock{mutex_};
        if (!pendingLaunch_)
            return std::nullopt;
        return ui::CloudApplicationPasswordRequest{pendingLaunch_->card.streamId, pendingLaunch_->card.name};
    }

    void SubmitPassword(const std::string& streamId, std::string password) override {
        std::optional<PendingLaunch> launch{};
        {
            const std::scoped_lock lock{mutex_};
            if (!pendingLaunch_ || pendingLaunch_->card.streamId != streamId)
                return;
            launch = std::move(pendingLaunch_);
            pendingLaunch_.reset();
        }
        Launch(std::move(*launch), std::move(password));
    }

    void CancelPassword(const std::string& streamId) override {
        const std::scoped_lock lock{mutex_};
        if (pendingLaunch_ && pendingLaunch_->card.streamId == streamId)
            pendingLaunch_.reset();
    }

    void Stop(const std::string& streamId) override {
        std::string instanceId{};
        std::string sessionId{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = instanceIds_.find(streamId); found != instanceIds_.end())
                instanceId = found->second;
            if (const auto found = activeSessions_.find(streamId); found != activeSessions_.end())
                sessionId = found->second;
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        static_cast<void>(
            runtime_->Worker()->Post([runtime, weakSelf, streamId, instanceId = std::move(instanceId), sessionId = std::move(sessionId)] {
                if (!sessionId.empty())
                    static_cast<void>(runtime->Launcher()->Stop(sessionId));
                const bool stopped = !instanceId.empty() && runtime->Console()->StopApplication(instanceId);
                const auto self = weakSelf.lock();
                if (!self)
                    return;
                if (!stopped)
                    runtime->Notify(true, "Application failed", "Console did not stop the application");
                const std::scoped_lock lock{self->mutex_};
                self->instanceIds_.erase(streamId);
                self->activeSessions_.erase(streamId);
                if (const auto found = std::ranges::find(self->cards_, streamId, &ui::CloudApplicationCard::streamId); found != self->cards_.end()) {
                    found->instanceState = "stopped";
                }
            }));
    }

    void SetForceTcp(const std::string& streamId, const bool enabled) override {
        SetPreference(streamId, enabled, false);
    }
    void SetForceRelay(const std::string& streamId, const bool enabled) override {
        SetPreference(streamId, false, enabled);
    }

  private:
    struct PendingLaunch final {
        ui::CloudApplicationCard card{};
        px_console::ConsoleNativeApplicationConnection connection{};
        std::string instanceId{};
        std::string nonce{};
        std::string sessionId{};
        bool viewOnly{};
    };

    void Launch(PendingLaunch launch, std::string password) {
        const auto runtime = runtime_;
        const auto vault = credentialVault_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, vault, weakSelf, launch = std::move(launch), password = std::move(password)]() mutable {
            const std::string passwordHash{MD5::Hex(password)};
            if (!launch.connection.rdp_configuration) {
                const auto verified = RenderApi::VerifySecurityPassword(launch.connection.host, launch.connection.port, passwordHash);
                if (!verified) {
                    runtime->Notify(
                        true, "Application failed",
                        "The application is running, but its Render control endpoint could not verify the password. The saved password was kept.");
                    return;
                }
                if (!verified.value()) {
                    vault->Delete("device:" + launch.connection.device_id);
                    runtime->Notify(true, "Application failed",
                                    "The Render device rejected this password. Enter the current password shown on that device and retry.");
                    return;
                }
            }
            const bool launched = runtime->Launcher()->Launch(
                {.connectionKind = launch.connection.rdp_configuration ? NativeConnectionKind::Rdp : NativeConnectionKind::IpDirect,
                 .displayName = launch.card.name,
                 .remoteDeviceId = launch.connection.device_id,
                 .instanceId = launch.instanceId,
                 .nonce = launch.nonce,
                 .directHost = launch.connection.host,
                 .directPort = launch.connection.port,
                 .directStreamId = launch.sessionId,
                 .remotePasswordHash = passwordHash,
                 .relayHost = launch.connection.relay_host,
                 .relayPort = launch.connection.relay_port,
                 .relayRemoteDeviceId = launch.connection.signal_device_id,
                 .rdpConfiguration = launch.connection.rdp_configuration,
                 .viewOnly = launch.viewOnly,
                 .forceTcp = launch.card.forceTcp,
                 .forceRelay = launch.card.forceRelay});
            const auto self = weakSelf.lock();
            if (!self)
                return;
            if (!launched) {
                self->SetInstanceState(launch.card.streamId, "running");
                runtime->Notify(
                    true, "Application failed",
                    "The application is running and authorization succeeded, but px_client could not start. Check the local installation.");
                return;
            }
            if (!password.empty())
                static_cast<void>(vault->Write("device:" + launch.connection.device_id, password));
            const std::scoped_lock lock{self->mutex_};
            self->instanceIds_[launch.card.streamId] = launch.instanceId;
            self->activeSessions_[launch.card.streamId] = launch.sessionId;
            if (const auto found = std::ranges::find(self->cards_, launch.card.streamId, &ui::CloudApplicationCard::streamId);
                found != self->cards_.end()) {
                found->instanceState = "running";
            }
        }));
    }

    void SetInstanceState(const std::string& streamId, const std::string& state) {
        const std::scoped_lock lock{mutex_};
        if (const auto found = std::ranges::find(cards_, streamId, &ui::CloudApplicationCard::streamId); found != cards_.end()) {
            found->instanceState = state;
        }
    }

    void SetPreference(const std::string& streamId, const bool tcp, const bool relay) {
        if (!runtime_->Config()->SaveCloudApplicationPreference(streamId, {.forceTcp = tcp, .forceRelay = relay})) {
            runtime_->Notify(true, "Pixels", "Unable to save application settings");
            return;
        }
        const std::scoped_lock lock{mutex_};
        preferences_[streamId] = {tcp, relay};
        if (const auto found = std::ranges::find(cards_, streamId, &ui::CloudApplicationCard::streamId); found != cards_.end()) {
            found->forceTcp = tcp;
            found->forceRelay = relay;
        }
    }

    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::shared_ptr<PanelCredentialVault> credentialVault_{};
    mutable std::mutex mutex_{};
    std::vector<ui::CloudApplicationCard> cards_{};
    std::unordered_map<std::string, std::pair<bool, bool>> preferences_{};
    std::unordered_map<std::string, std::string> instanceIds_{};
    std::unordered_map<std::string, std::string> activeSessions_{};
    std::optional<PendingLaunch> pendingLaunch_{};
};

} // namespace

std::shared_ptr<ui::CloudApplicationsPort> CreateProductCloudApplicationsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    auto result = std::make_shared<ProductCloudApplicationsPort>(runtime);
    result->Initialize();
    return result;
}

} // namespace px::panel::product
