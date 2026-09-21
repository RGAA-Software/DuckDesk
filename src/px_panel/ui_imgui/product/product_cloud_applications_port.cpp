#include <chrono>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include "panel_product_runtime.h"
#include "px_common/uuid.h"
#include "px_console_client/console_errors.h"
#include "px_ui/product_brand.h"

namespace px::panel::product {
namespace {

ui::CloudApplicationKind ResolveApplicationKind(const std::string_view appType) noexcept {
    if (appType == "game_hook") return ui::CloudApplicationKind::Game;
    if (appType == "webview") return ui::CloudApplicationKind::WebView;
    if (appType == "rdp") return ui::CloudApplicationKind::Rdp;
    return ui::CloudApplicationKind::Remote;
}

class ProductCloudApplicationsPort final : public ui::CloudApplicationsPort, public std::enable_shared_from_this<ProductCloudApplicationsPort> {
public:
    explicit ProductCloudApplicationsPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {}
    void Initialize() { Refresh(); }

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
            if (!self) return;
            std::vector<ui::CloudApplicationCard> cards{};
            std::unordered_map<std::string, std::string> instances{};
            std::unordered_map<std::string, std::pair<bool, bool>> preferences{};
            {
                const std::scoped_lock lock{self->mutex_};
                preferences = self->preferences_;
            }
            for (const auto& application : applications) {
                const bool rdpMode{application.app_type == "rdp"};
                const auto inMemory = preferences.find(application.app_id);
                const auto persisted = runtime->Config()->LoadCloudApplicationPreference(application.app_id);
                const bool forceTcp{!rdpMode && (inMemory != preferences.end() ? inMemory->second.first : persisted.forceTcp)};
                const bool forceRelay{!rdpMode && (inMemory != preferences.end() ? inMemory->second.second : persisted.forceRelay)};
                cards.push_back({.streamId = application.app_id,
                                 .name = application.name,
                                 .instanceState = application.running_instance ? application.running_instance->state : "stopped",
                                 .kind = ResolveApplicationKind(application.app_type),
                                 .rdpMode = rdpMode,
                                 .forceTcp = forceTcp,
                                 .forceRelay = forceRelay});
                if (application.running_instance) instances[application.app_id] = application.running_instance->instance_id;
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
            if (found == cards_.end()) return;
            card = *found;
            if (const auto instance = instanceIds_.find(streamId); instance != instanceIds_.end()) existingInstance = instance->second;
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
                if (!self) return;
                const std::string nonce{GetUUID()};
                std::string instanceId{existingInstance};
                std::string instanceState{existingInstance.empty() ? std::string{} : card.instanceState};
                if (!instanceId.empty()) {
                    const auto applications = runtime->Console()->QueryApplications();
                    const auto current = std::ranges::find(applications, card.streamId, &px_console::ConsoleUserApplication::app_id);
                    if (current != applications.end() && current->running_instance &&
                        (current->running_instance->state == "starting" || current->running_instance->state == "running")) {
                        instanceId = current->running_instance->instance_id;
                        instanceState = current->running_instance->state;
                    } else {
                        instanceId.clear();
                        instanceState.clear();
                    }
                }
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
                auto connection = runtime->Console()->QueryNativeApplicationConnection(instanceId, viewOnly, nonce);
                for (int attempt{}; !connection && attempt < 40; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{500});
                    connection = runtime->Console()->QueryNativeApplicationConnection(instanceId, viewOnly, nonce);
                }
                if (!connection) {
                    self->SetInstanceState(card.streamId, "running");
                    const std::string serverMessage{px_console::ConsoleApiLastErrorMessage()};
                    runtime->Notify(true, "Application failed",
                                    serverMessage.empty() ? px_console::ConsoleApiErrorAsString(connection.error()) : serverMessage);
                    return;
                }
                const auto sessionId = connection->session_id;
                PendingLaunch launch{.card = card,
                                     .connection = std::move(connection.value()),
                                     .instanceId = instanceId,
                                     .nonce = nonce,
                                     .sessionId = sessionId,
                                     .viewOnly = viewOnly};
                self->Launch(std::move(launch));
            }));
    }

    std::optional<ui::CloudApplicationPasswordRequest> PendingPasswordRequest() const override { return std::nullopt; }

    void SubmitPassword(const std::string& /*streamId*/, std::string /*password*/) override {}

    void CancelPassword(const std::string& /*streamId*/) override {}

    void Stop(const std::string& streamId) override {
        std::string instanceId{};
        ActiveSession activeSession{};
        {
            const std::scoped_lock lock{mutex_};
            if (const auto found = instanceIds_.find(streamId); found != instanceIds_.end()) instanceId = found->second;
            if (const auto found = activeSessions_.find(streamId); found != activeSessions_.end()) activeSession = found->second;
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        static_cast<void>(
            runtime_->Worker()->Post([runtime, weakSelf, streamId, instanceId = std::move(instanceId), activeSession = std::move(activeSession)] {
                if (!activeSession.id.empty()) {
                    static_cast<void>(runtime->Console()->CloseResourceConnection(activeSession.id, activeSession.revision));
                    static_cast<void>(runtime->Launcher()->Stop(activeSession.id));
                }
                const bool stopped = !instanceId.empty() && runtime->Console()->StopApplication(instanceId);
                const auto self = weakSelf.lock();
                if (!self) return;
                if (!stopped) runtime->Notify(true, "Application failed", "Console did not stop the application");
                const std::scoped_lock lock{self->mutex_};
                self->instanceIds_.erase(streamId);
                self->activeSessions_.erase(streamId);
                if (const auto found = std::ranges::find(self->cards_, streamId, &ui::CloudApplicationCard::streamId); found != self->cards_.end()) {
                    found->instanceState = "stopped";
                }
            }));
    }

    void SetForceTcp(const std::string& streamId, const bool enabled) override { SetPreference(streamId, enabled, false); }
    void SetForceRelay(const std::string& streamId, const bool enabled) override { SetPreference(streamId, false, enabled); }

private:
    struct ActiveSession final {
        std::string id{};
        std::int64_t revision{};
    };

    struct PendingLaunch final {
        ui::CloudApplicationCard card{};
        px_console::ConsoleNativeApplicationConnection connection{};
        std::string instanceId{};
        std::string nonce{};
        std::string sessionId{};
        bool viewOnly{};
    };

    void Launch(PendingLaunch launch) {
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        static_cast<void>(runtime_->Worker()->Post([runtime, weakSelf, launch = std::move(launch)]() mutable {
            if (launch.card.forceRelay &&
                (launch.connection.relay_host.empty() || launch.connection.relay_port <= 0 || launch.connection.relay_admission_ticket.empty())) {
                if (const auto self = weakSelf.lock()) {
                    self->SetInstanceState(launch.card.streamId, "running");
                }
                runtime->Notify(true, "Application failed", "Console did not issue a Relay route for this resource session.");
                return;
            }
            const bool rdp = launch.connection.transport == "rdp";
            const bool launched = runtime->Launcher()->Launch({.connectionKind = rdp ? NativeConnectionKind::Rdp : NativeConnectionKind::IpDirect,
                                                               .displayName = launch.card.name,
                                                               .remoteDeviceId = launch.connection.device_id,
                                                               .instanceId = launch.instanceId,
                                                               .nonce = launch.nonce,
                                                               .directHost = launch.connection.host,
                                                               .directPort = launch.connection.port,
                                                               .directStreamId = launch.sessionId,
                                                               .remotePasswordHash = {},
                                                               .frontendSessionId = launch.connection.session_id,
                                                               .frontendSessionRevision = launch.connection.session_revision,
                                                               .frontendToken = launch.connection.frontend_token,
                                                               .relayHost = launch.connection.relay_host,
                                                               .relayPort = launch.connection.relay_port,
                                                               .relayRemoteDeviceId = "server_" + launch.connection.device_id,
                                                               .relayAdmissionTicket = launch.connection.relay_admission_ticket,
                                                               .rdpConfiguration = launch.connection.rdp_configuration,
                                                               .viewOnly = launch.viewOnly,
                                                               .forceTcp = launch.card.forceTcp,
                                                               .forceRelay = launch.card.forceRelay});
            const auto self = weakSelf.lock();
            if (!self) return;
            if (!launched) {
                self->SetInstanceState(launch.card.streamId, "running");
                runtime->Notify(
                    true, "Application failed",
                    "The application is running and authorization succeeded, but px_client could not start. Check the local installation.");
                return;
            }
            const std::scoped_lock lock{self->mutex_};
            self->instanceIds_[launch.card.streamId] = launch.instanceId;
            self->activeSessions_[launch.card.streamId] = ActiveSession{.id = launch.sessionId, .revision = launch.connection.session_revision};
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
            runtime_->Notify(true, std::string{px::ui::ApplicationName()}, "Unable to save application settings");
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
    mutable std::mutex mutex_{};
    std::vector<ui::CloudApplicationCard> cards_{};
    std::unordered_map<std::string, std::pair<bool, bool>> preferences_{};
    std::unordered_map<std::string, std::string> instanceIds_{};
    std::unordered_map<std::string, ActiveSession> activeSessions_{};
};

}  // namespace

std::shared_ptr<ui::CloudApplicationsPort> CreateProductCloudApplicationsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    auto result = std::make_shared<ProductCloudApplicationsPort>(runtime);
    result->Initialize();
    return result;
}

}  // namespace px::panel::product
