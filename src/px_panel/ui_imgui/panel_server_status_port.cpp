#include "panel_server_status_port.h"

#include "render_panel/px_app_messages.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/px_render_controller.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_statistics.h"

#include "px_common/ip_util.h"
#include "px_common/message_notifier.h"

#include <atomic>
#include <functional>
#include <utility>

namespace px::panel::ui {
namespace {

class PanelServerStatusPort final : public ServerStatusPort, public std::enable_shared_from_this<PanelServerStatusPort> {
  public:
    static std::shared_ptr<PanelServerStatusPort> Create(const std::shared_ptr<PxApplication>& application) {
        auto port = std::make_shared<PanelServerStatusPort>(application);
        port->RegisterMessages();
        return port;
    }

    explicit PanelServerStatusPort(const std::shared_ptr<PxApplication>& application) : application_{application} {
        if (const auto context = application->GetContext()) {
            for (const auto& address : context->GetIps()) {
                addresses_.push_back({.address = address.ip_addr_, .wired = address.nt_type_ == IPNetworkType::kWired});
            }
        }
    }

    ServerStatusState Snapshot() const override {
        ServerStatusState state{};
        const auto application = application_.lock();
        if (!application) {
            return state;
        }
        state.controllerDriverReady = controllerDriverReady_.load();
        state.renderReady = application->IsRendererConnected();
        state.serviceReady = application->IsServiceConnected();
        state.addresses = addresses_;
        const std::reference_wrapper<PxSettings> settings{*PxSettings::Instance()};
        state.panelPort = settings.get().GetPanelServerPort();
        state.renderPort = settings.get().GetRenderServerPort();
        const auto statistics = PxStatistics::Instance();
        state.audioSamples = statistics->audio_samples_.load();
        state.audioChannels = statistics->audio_channels_.load();
        state.audioBits = statistics->audio_bits_.load();
        return state;
    }

    void RestartRender() override {
        const auto application = application_.lock();
        const auto context = application ? application->GetContext() : std::shared_ptr<PxContext>{};
        const auto controller = context ? context->GetRenderController() : std::shared_ptr<PxRenderController>{};
        if (!context || !controller) {
            return;
        }
        const std::weak_ptr<PxContext> weakContext{context};
        const std::weak_ptr<PxRenderController> weakController{controller};
        context->PostTask([weakContext, weakController] {
            const auto lockedContext = weakContext.lock();
            const auto lockedController = weakController.lock();
            if (!lockedContext || !lockedController) {
                return;
            }
            lockedController->ReStart();
            lockedContext->SendAppMessage(MsgServerAlive{.alive_ = false});
        });
    }

    void InstallControllerDriver() override {
        if (const auto application = application_.lock()) {
            if (const auto context = application->GetContext()) {
                context->SendAppMessage(MsgInstallViGEm{});
            }
        }
    }

  private:
    void RegisterMessages() {
        const auto application = application_.lock();
        const auto context = application ? application->GetContext() : std::shared_ptr<PxContext>{};
        if (!context) {
            return;
        }
        messageListener_ = context->ObtainMessageListener();
        if (!messageListener_) {
            return;
        }
        const std::weak_ptr<PanelServerStatusPort> weakSelf{shared_from_this()};
        messageListener_->Listen<MsgViGEmState>([weakSelf](const MsgViGEmState& state) {
            if (const auto self = weakSelf.lock()) {
                self->controllerDriverReady_.store(state.ok_);
            }
        });
    }

    std::weak_ptr<PxApplication> application_{};
    std::shared_ptr<MessageListener> messageListener_{};
    std::atomic_bool controllerDriverReady_{false};
    std::vector<NetworkAddress> addresses_{};
};

} // namespace

std::shared_ptr<ServerStatusPort> CreatePanelServerStatusPort(const std::shared_ptr<PxApplication>& application) {
    return PanelServerStatusPort::Create(application);
}

} // namespace px::panel::ui
