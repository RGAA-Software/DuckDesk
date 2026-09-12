#include "panel_network_settings_controller.h"

#include "render_panel/companion/panel_companion.h"
#include "render_panel/devices/px_device_manager.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/px_settings.h"

#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/string_util.h"
#include "px_console_client/console_device.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <utility>
#include <vector>

namespace px {

std::shared_ptr<PanelNetworkSettingsController> PanelNetworkSettingsController::Create(const std::shared_ptr<PxApplication>& application) {
    auto controller = std::make_shared<PanelNetworkSettingsController>(application);
    if (const auto context = application->GetContext(); context && context->GetMessageNotifier()) {
        controller->requestScope_ = PxAsyncScope::Create(context->GetMessageNotifier()->GetAsyncRuntime(), PxAsyncLane::kControl);
    }
    controller->ParseAuthorization(controller->state_.settings.authorizationInfo);
    return controller;
}

PanelNetworkSettingsController::PanelNetworkSettingsController(const std::shared_ptr<PxApplication>& application)
    : application_{application}, settings_{*PxSettings::Instance()}, verifyGate_{LatestSerialRequestGate::Create()},
      saveGate_{LatestSerialRequestGate::Create()} {
    auto& settings = settings_.get();
    state_.settings = {
        .authorizationInfo = settings.GetConsoleAccessInfo(),
        .nodePublicAddress = settings.GetNodeAccessHost(),
        .serviceManagementPort = settings.GetServiceServerPort(),
        .desktopConnectionPort = settings.GetRenderServerPort(),
        .applicationPorts = {settings.GetApplicationPortStart(), settings.GetApplicationPortEnd()},
        .rtcPorts = {settings.GetRtcPortStart(), settings.GetRtcPortEnd()},
        .panelListeningPort = settings.GetPanelServerPort(),
    };
}

PanelNetworkSettingsController::~PanelNetworkSettingsController() {
    Stop();
}

PanelNetworkState PanelNetworkSettingsController::Snapshot() const {
    const std::scoped_lock lock{stateMutex_};
    return state_;
}

std::optional<NetworkEndpointRequest> PanelNetworkSettingsController::ParseEndpoint(const std::string& authorizationInfo) {
    const auto companion = application_->GetCompanionShared();
    const auto access = companion ? companion->ParseConsoleAccessInfo(authorizationInfo) : nullptr;
    if (!access || !access->console_config_.IsValid()) {
        return std::nullopt;
    }
    return NetworkEndpointRequest{
        .host = access->console_config_.srv_w3c_ip_,
        .consolePort = access->console_config_.srv_console_port_,
        .appkey = access->console_config_.srv_appkey_,
    };
}

void PanelNetworkSettingsController::ParseAuthorization(std::string authorizationInfo) {
    const auto endpoint = ParseEndpoint(authorizationInfo);
    const auto companion = application_->GetCompanionShared();
    const auto access = companion ? companion->ParseConsoleAccessInfo(authorizationInfo) : nullptr;
    const std::scoped_lock lock{stateMutex_};
    state_.settings.authorizationInfo = std::move(authorizationInfo);
    state_.settings.consolePort = endpoint ? std::optional{endpoint->consolePort} : std::nullopt;
    state_.settings.relayPort = access && access->console_config_.IsValid() ? std::optional{access->console_config_.srv_relay_port_} : std::nullopt;
    state_.operation = endpoint ? PanelNetworkOperation::Idle : PanelNetworkOperation::InvalidAuthorization;
    state_.detail.clear();
}

void PanelNetworkSettingsController::Verify(std::string authorizationInfo) {
    const auto endpoint = ParseEndpoint(authorizationInfo);
    if (!endpoint || !requestScope_) {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::InvalidAuthorization;
        return;
    }
    ParseAuthorization(std::move(authorizationInfo));
    const auto request = verifyGate_->Begin();
    if (!request) {
        return;
    }
    {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::Verifying;
    }
    const auto context = application_->GetContext();
    const PxBlockingTaskPoster poster = [context](std::function<void()> task) { context->PostNetworkTask(std::move(task)); };
    const auto weakSelf = weak_from_this();
    const bool spawned = requestScope_->Spawn("panel-network-verify", [gate = verifyGate_, request, poster, endpoint = *endpoint, weakSelf]() {
        return RunVerifyNetwork(gate, request, poster, endpoint,
                                [gate, generation = request.generation, weakSelf, endpoint](VerifyNetworkResult result) {
                                    if (!gate->Complete(generation)) {
                                        return;
                                    }
                                    if (const auto self = weakSelf.lock()) {
                                        self->CompleteVerify(endpoint, std::move(result));
                                    }
                                });
    });
    if (!spawned) {
        request.cancellation->store(true, std::memory_order_release);
        static_cast<void>(verifyGate_->Complete(request.generation));
    }
}

void PanelNetworkSettingsController::CompleteVerify(const NetworkEndpointRequest&, VerifyNetworkResult result) {
    const std::scoped_lock lock{stateMutex_};
    if (result.failure == VerifyNetworkResult::Failure::None) {
        state_.operation = PanelNetworkOperation::Verified;
        state_.detail.clear();
        return;
    }
    state_.operation = PanelNetworkOperation::Failed;
    state_.detail = result.failure == VerifyNetworkResult::Failure::Async ? result.asyncError.message : result.consoleMessage;
}

std::string PanelNetworkSettingsController::DefaultDeviceName() const {
    const auto context = application_->GetContext();
    const auto ips = context ? context->GetIps() : std::vector<EthernetInfo>{};
    if (ips.empty()) {
        return "D-NULL";
    }
    std::vector<std::string> segments;
    StringUtil::Split(ips.front().ip_addr_, segments, ".");
    return segments.empty() ? "D-NULL" : "D-" + segments.back();
}

void PanelNetworkSettingsController::Save(std::string authorizationInfo, std::string nodePublicAddress) {
    const auto endpoint = ParseEndpoint(authorizationInfo);
    const auto companion = application_->GetCompanionShared();
    const auto access = companion ? companion->ParseConsoleAccessInfo(authorizationInfo) : nullptr;
    if (!endpoint || !access) {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::InvalidAuthorization;
        return;
    }
    if (!IsValidNodeAccessHost(nodePublicAddress)) {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::InvalidPublicAddress;
        return;
    }
    if (!requestScope_) {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::Failed;
        state_.detail = "network runtime unavailable";
        return;
    }

    auto& settings = settings_.get();
    const bool forceUpdateDeviceId{settings.GetConsoleServerHost() != endpoint->host || settings.GetConsoleServerPort() != endpoint->consolePort};
    if (forceUpdateDeviceId) {
        settings.SetDeviceId("");
        companion->UpdateDeviceId("");
        settings.SetDeviceName("");
        settings.SetDeviceRandomPwd("");
    }
    settings.SetConsoleServerHost(endpoint->host);
    settings.SetConsoleServerPort(std::to_string(endpoint->consolePort));
    settings.SetNodeAccessHost(nodePublicAddress);
    settings.SetRelayServerHost(access->console_config_.srv_w3c_ip_);
    settings.SetRelayServerPort(std::to_string(access->console_config_.srv_relay_port_));
    settings.SetConsoleAccessInfo(authorizationInfo);
    settings.SetConsoleSslEnabled(true);
    settings.Load();
    companion->UpdateConsoleServerConfig(settings.GetConsoleServerHost(), settings.GetConsoleServerPort(), settings.IsConsoleSslEnabled());
    companion->UpdateAppkey(endpoint->appkey);
    application_->RefreshClientManagerSettings();

    const auto request = saveGate_->Begin();
    if (!request) {
        return;
    }
    {
        const std::scoped_lock lock{stateMutex_};
        state_.settings.authorizationInfo = std::move(authorizationInfo);
        state_.settings.nodePublicAddress = std::move(nodePublicAddress);
        state_.settings.consolePort = endpoint->consolePort;
        state_.settings.relayPort = access->console_config_.srv_relay_port_;
        state_.operation = PanelNetworkOperation::Saving;
        state_.detail.clear();
    }
    const auto context = application_->GetContext();
    const PxBlockingTaskPoster poster = [context](std::function<void()> task) { context->PostNetworkTask(std::move(task)); };
    const auto weakSelf = weak_from_this();
    const auto deviceManager = application_->GetDeviceManager();
    const auto deviceId = settings.GetDeviceId();
    const bool spawned =
        requestScope_->Spawn("panel-network-save", [gate = saveGate_, request, poster, endpoint = *endpoint, deviceManager, deviceId,
                                                    defaultDeviceName = DefaultDeviceName(), weakSelf, forceUpdateDeviceId]() mutable {
            return RunSaveNetwork(gate, request, poster, deviceManager, std::move(deviceId), std::move(defaultDeviceName),
                                  [gate, generation = request.generation, weakSelf, endpoint, forceUpdateDeviceId](SaveNetworkResult result) {
                                      if (!gate->Complete(generation)) {
                                          return;
                                      }
                                      if (const auto self = weakSelf.lock()) {
                                          self->CompleteSave(endpoint, forceUpdateDeviceId, std::move(result));
                                      }
                                  });
        });
    if (!spawned) {
        request.cancellation->store(true, std::memory_order_release);
        static_cast<void>(saveGate_->Complete(request.generation));
    }
}

void PanelNetworkSettingsController::CompleteSave(const NetworkEndpointRequest&, const bool forceUpdateDeviceId, SaveNetworkResult result) {
    if (result.failure != SaveNetworkResult::Failure::None) {
        const std::scoped_lock lock{stateMutex_};
        state_.operation = PanelNetworkOperation::Failed;
        state_.detail = result.failure == SaveNetworkResult::Failure::Async ? result.asyncError.message : "device registration failed";
        return;
    }
    const auto context = application_->GetContext();
    if (result.newDevice) {
        auto& settings = settings_.get();
        settings.SetDeviceId(result.newDevice->device_id_);
        settings.SetDeviceName(result.newDevice->device_name_);
        settings.SetDeviceRandomPwd(result.newDevice->gen_random_pwd_);
        if (const auto companion = application_->GetCompanionShared()) {
            companion->UpdateDeviceId(result.newDevice->device_id_);
        }
        context->SendAppMessage(MsgRequestedNewDevice{
            .device_id_ = result.newDevice->device_id_,
            .device_random_pwd_ = result.newDevice->gen_random_pwd_,
            .force_update_ = true,
        });
        context->SendAppMessage(MsgSyncSettingsToRender{});
    }
    context->SendAppMessage(MsgSettingsChanged{.settings_ = PxSettings::Instance(), .force_update_device_id_ = forceUpdateDeviceId});
    const std::scoped_lock lock{stateMutex_};
    state_.operation = PanelNetworkOperation::SavedNeedsRestart;
    state_.detail.clear();
}

void PanelNetworkSettingsController::RestartRender() {
    if (const auto context = application_->GetContext()) {
        context->SendAppMessage(AppMsgRestartServer{});
    }
    const std::scoped_lock lock{stateMutex_};
    state_.operation = PanelNetworkOperation::Idle;
}

void PanelNetworkSettingsController::Acknowledge() {
    const std::scoped_lock lock{stateMutex_};
    state_.operation = PanelNetworkOperation::Idle;
}

void PanelNetworkSettingsController::Stop() {
    verifyGate_->Stop();
    saveGate_->Stop();
    if (requestScope_) {
        static_cast<void>(requestScope_->StopAndWait(std::chrono::seconds(2)));
        requestScope_.reset();
    }
}

} // namespace px
