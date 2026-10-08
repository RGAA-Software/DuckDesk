#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include "application_launch_workflow.h"
#include "panel_product_runtime.h"
#include "px_common/uuid.h"
#include "px_console_client/console_errors.h"
#include "px_ui/localization.h"
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
    void Initialize() {
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        workflow_ = std::make_shared<ApplicationLaunchWorkflow>(ApplicationLaunchOperations{
            .start = [runtime](const std::string& applicationId,
                               const std::string& requestId) { return runtime->Console()->StartApplication(applicationId, requestId); },
            .query = [runtime](const std::string& instanceId) { return runtime->Console()->QueryApplicationInstance(instanceId); },
            .authorize =
                [runtime](const std::string& instanceId, const bool viewOnly, const std::string& requestId) {
                    return runtime->Console()->QueryNativeApplicationConnection(instanceId, viewOnly, requestId);
                },
            .clientAvailable =
                [runtime] {
                    std::error_code error{};
                    return std::filesystem::is_regular_file(runtime->Config()->ExecutableDirectory() / "px_client.exe", error);
                },
            .launch =
                [weakSelf](const ApplicationLaunchRequest& request, const px_console::ConsoleNativeApplicationConnection& connection) {
                    const auto self = weakSelf.lock();
                    return self && self->LaunchClient(request, connection);
                },
            .close = [runtime](
                         const std::string& sessionId,
                         const std::int64_t revision) { static_cast<void>(runtime->Console()->CloseResourceConnection(sessionId, revision)); }});
        Refresh();
    }

    std::vector<ui::CloudApplicationCard> Snapshot() override {
        bool refreshDue{};
        {
            const std::scoped_lock lock{mutex_};
            refreshDue = std::chrono::steady_clock::now() >= nextRefresh_;
        }
        if (refreshDue) Refresh();
        const std::scoped_lock lock{mutex_};
        return cards_;
    }

    void Refresh() override {
        if (refreshPending_.exchange(true)) return;
        {
            const std::scoped_lock lock{mutex_};
            nextRefresh_ = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        }
        const auto runtime = runtime_;
        const std::weak_ptr<ProductCloudApplicationsPort> weakSelf{shared_from_this()};
        const bool posted = runtime_->Worker()->Post([runtime, weakSelf] {
            const auto applications = runtime->Console()->QueryApplications();
            const auto self = weakSelf.lock();
            if (!self) return;
            self->refreshPending_.store(false);
            if (!applications) {
                if (!self->refreshFailed_.exchange(true)) self->NotifyQueryFailure();
                return;
            }
            self->refreshFailed_.store(false);
            std::vector<ui::CloudApplicationCard> cards{};
            std::unordered_map<std::string, std::string> instances{};
            std::unordered_map<std::string, std::pair<bool, bool>> preferences{};
            {
                const std::scoped_lock lock{self->mutex_};
                preferences = self->preferences_;
            }
            for (const auto& application : applications.value()) {
                const bool rdpMode{application.app_type == "rdp"};
                const auto inMemory = preferences.find(application.app_id);
                const auto persisted = runtime->Config()->LoadCloudApplicationPreference(application.app_id);
                const bool forceTcp{!rdpMode && (inMemory != preferences.end() ? inMemory->second.first : persisted.forceTcp)};
                const bool forceRelay{!rdpMode && (inMemory != preferences.end() ? inMemory->second.second : persisted.forceRelay)};
                cards.push_back({.streamId = application.app_id,
                                 .name = application.name,
                                 .instanceState = application.running_instance ? application.running_instance->state : "stopped",
                                 .runningInstanceCount = application.running_instance_count,
                                 .kind = ResolveApplicationKind(application.app_type),
                                 .rdpMode = rdpMode,
                                 .forceTcp = forceTcp,
                                 .forceRelay = forceRelay});
                if (application.running_instance) instances[application.app_id] = application.running_instance->instance_id;
            }
            const std::scoped_lock lock{self->mutex_};
            self->cards_ = std::move(cards);
            self->instanceIds_ = std::move(instances);
        });
        if (!posted) refreshPending_.store(false);
    }

    void Start(const std::string& streamId, const bool viewOnly) override {
        ApplicationLaunchRequest request{};
        {
            const std::scoped_lock lock{mutex_};
            const auto application = std::ranges::find(cards_, streamId, &ui::CloudApplicationCard::streamId);
            if (application == cards_.end()) return;
            request = {.applicationId = streamId,
                       .applicationName = application->name,
                       .requestId = GetCanonicalUUID(),
                       .viewOnly = viewOnly,
                       .forceTcp = application->forceTcp,
                       .forceRelay = application->forceRelay};
            if (const auto instance = instanceIds_.find(streamId); instance != instanceIds_.end()) request.existingInstanceId = instance->second;
        }
        const auto generation = workflow_->Begin(std::move(request));
        if (!generation) return;
        const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow_};
        if (!runtime_->Worker()->Post([weakWorkflow, generation = *generation] {
                if (const auto workflow = weakWorkflow.lock()) workflow->Prepare(generation);
            }))
            workflow_->WorkerUnavailable(*generation);
    }

    std::optional<ui::ApplicationLaunchProgress> LaunchProgress() const override { return workflow_->Snapshot(); }

    void LaunchPrepared(const std::uint64_t generation) override {
        const std::weak_ptr<ApplicationLaunchWorkflow> weakWorkflow{workflow_};
        if (!runtime_->Worker()->Post([weakWorkflow, generation] {
                if (const auto workflow = weakWorkflow.lock()) workflow->LaunchPrepared(generation);
            }))
            workflow_->WorkerUnavailable(generation);
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
    void NotifyQueryFailure() const {
        const px::ui::Localizer localizer{runtime_->Config()->Settings().language};
        runtime_->Notify(true, std::string{localizer.Text(px::ui::TextId::CloudApplications)},
                         std::string{localizer.Text(px::ui::TextId::CloudApplicationRefreshFailed)});
    }

    struct ActiveSession final {
        std::string id{};
        std::int64_t revision{};
    };

    bool LaunchClient(const ApplicationLaunchRequest& request, const px_console::ConsoleNativeApplicationConnection& connection) {
        const auto runtime = runtime_;
        const bool rdp = connection.transport == "rdp";
        const bool launched = runtime->Launcher()->Launch({.connectionKind = rdp ? NativeConnectionKind::Rdp : NativeConnectionKind::IpDirect,
                                                           .displayName = request.applicationName,
                                                           .remoteDeviceId = connection.device_id,
                                                           .instanceId = connection.instance_id,
                                                           .nonce = request.requestId,
                                                           .directHost = connection.host,
                                                           .directPort = connection.port,
                                                           .directStreamId = connection.session_id,
                                                           .remotePasswordHash = {},
                                                           .frontendSessionId = connection.session_id,
                                                           .frontendSessionRevision = connection.session_revision,
                                                           .frontendToken = connection.frontend_token,
                                                           .relayHost = connection.relay_host,
                                                           .relayPort = connection.relay_port,
                                                           .relayRemoteDeviceId = "server_" + connection.device_id,
                                                           .relayAdmissionTicket = connection.relay_admission_ticket,
                                                           .rdpConfiguration = connection.rdp_configuration,
                                                           .viewOnly = request.viewOnly,
                                                           .forceTcp = request.forceTcp,
                                                           .forceRelay = request.forceRelay});
        if (!launched) return false;
        {
            const std::scoped_lock lock{mutex_};
            instanceIds_[request.applicationId] = connection.instance_id;
            activeSessions_[request.applicationId] = ActiveSession{.id = connection.session_id, .revision = connection.session_revision};
        }
        Refresh();
        return true;
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
    std::shared_ptr<ApplicationLaunchWorkflow> workflow_{};
    std::atomic<bool> refreshFailed_{};
    std::atomic_bool refreshPending_{};
    std::chrono::steady_clock::time_point nextRefresh_{};
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
