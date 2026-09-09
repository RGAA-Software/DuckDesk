#include "rd_app.h"

#include "app/app_timer.h"
#include "modules/render_module_registry.h"
#include "network/render_service_client.h"
#include "px_common/log.h"
#include "px_common/win32/win_helper.h"
#include "px_rdp/rdp_proxy_process.h"
#include "px_rdp/rdp_control_lease.h"
#include "rd_statistics.h"
#include "settings/rd_settings.h"

namespace px {

int RdApplication::RunRdp() {
    // Establish the message queue before any worker can request shutdown or
    // post UI work. PostThreadMessage otherwise silently loses an early wakeup.
    main_thread_id_ = GetCurrentThreadId();
    MSG initial_message{};
    PeekMessageW(&initial_message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    // Separate composition: no native capture/encoder/file/input/audio/joystick
    // service is even constructed. Network admission still uses the product's
    // Render event channel, Console tickets and Service connection.
    settings_.rdp_launch_.proxy_directory = std::filesystem::path(WinHelper::GetExeFolderPath()) / "rdp";
    settings_.rdp_launch_.private_root = settings_.rdp_launch_.proxy_directory / "workspaces";
    std::string error{};
    auto proxy = rdp::RdpProxyProcess::Start(settings_.rdp_launch_, error);
    if (!proxy) {
        init_failed_ = true;
        init_error_ = error;
        LOGE("event=rdp.proxy.start outcome=failed reason={}", error);
        return -1;
    }
    rdp_proxy_.store(std::shared_ptr<rdp::RdpProxyProcess>(std::move(proxy)));
    rdApp = shared_from_this();
    module_registry_ = RenderModuleRegistry::Make(shared_from_this());
    context_->SetRenderModuleRegistry(module_registry_);
    module_registry_->StartModules();
    if (!module_registry_->IsRdpListenerReady()) {
        init_failed_ = true;
        init_error_ = "RDP product WebSocket listener failed";
        Exit();
        return -1;
    }
    module_registry_->BindIngressCallbacks();
    msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kControl);
    state_msg_listener_ = context_->CreateMessageListener(MessageExecutionLane::kState);
    InitConnectionLifecycle();
    game_hook_startup_grace_complete_ = true;
    const auto weak_self = weak_from_this();
    state_msg_listener_->Listen<MsgTimer1000>([weak_self, control = std::make_shared<rdp::RdpControlLease>()](const MsgTimer1000&) {
        if (const auto self = weak_self.lock(); self && !self->exit_app_) {
            const auto proxy = self->rdp_proxy_.load();
            if (!proxy || !proxy->IsAlive()) {
                LOGE("event=rdp.proxy.exit outcome=unexpected");
                self->Exit();
                return;
            }
            if (!control->Observe(self->service_client_ && self->service_client_->IsAlive(), rdp::RdpControlLease::Clock::now())) {
                LOGW("event=rdp.service.lost outcome=stop_runtime preserve_windows_session=true");
                self->Exit();
                return;
            }
            self->module_registry_->On1Second();
        }
    });
    service_client_ = std::make_shared<RenderServiceClient>(shared_from_this());
    service_client_->NotifyAppInstanceReady(settings_.rdp_launch_.instance_id, settings_.transmission_.listening_port_, true, "");
    service_client_->Start();
    InitAppTimer();
    // Same 45-second cold-start allowance as game-hook. Once a real client has
    // connected, the shared five-second disconnect-generation rule takes over.
    context_->PostDelayTask(
        [weak_self] {
            if (const auto self = weak_self.lock(); self && !self->exit_app_ && !self->game_hook_has_seen_client_ && !self->HasConnectedPeer()) {
                LOGI("event=rdp.startup.timeout outcome=stop_runtime preserve_windows_session=true");
                self->Exit();
            }
        },
        45000);
    LOGI("event=rdp.proxy.ready outcome=success capture=false reencode=false");
    return RunMessageLoop();
}

} // namespace px
