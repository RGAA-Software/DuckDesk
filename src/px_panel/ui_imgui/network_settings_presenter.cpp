#include "network_settings_presenter.h"

#include <imgui.h>

#include <utility>

#include "px_ui/components/button.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

namespace px::panel::ui {

NetworkSettingsPresenter::NetworkSettingsPresenter(std::shared_ptr<NetworkSettingsPort> port) : port_{std::move(port)} { Synchronize(); }

px::ui::TextId NetworkSettingsPresenter::StatusText(const NetworkOperation operation) const noexcept {
    switch (operation) {
        case NetworkOperation::InvalidConsoleAddress:
            return px::ui::TextId::InvalidConsoleAddress;
    case NetworkOperation::Verifying:
        return px::ui::TextId::Verifying;
    case NetworkOperation::Verified:
        return px::ui::TextId::Verified;
    case NetworkOperation::Saving:
        return px::ui::TextId::Saving;
    case NetworkOperation::SavedNeedsRestart:
        return px::ui::TextId::Saved;
    case NetworkOperation::Failed:
        return px::ui::TextId::OperationFailed;
    case NetworkOperation::Idle:
        return px::ui::TextId::PreviewInitialStatus;
    }
    return px::ui::TextId::OperationFailed;
}

void NetworkSettingsPresenter::Synchronize() {
    const auto state = port_->Snapshot();
    if (state.operation != lastOperation_) {
        page_.SetStatus(StatusText(state.operation));
        lastOperation_ = state.operation;
    }
    auto draft = page_.Draft();
    draft.consolePort = state.settings.consolePort;
    draft.serviceManagementPort = state.settings.serviceManagementPort;
    draft.desktopConnectionPort = state.settings.desktopConnectionPort;
    draft.applicationPorts = state.settings.applicationPorts;
    draft.rtcPorts = state.settings.rtcPorts;
    draft.panelListeningPort = state.settings.panelListeningPort;
    if (draft.consoleAddress.empty()) {
        draft.consoleAddress = state.settings.consoleAddress;
    }
    page_.SetDraft(std::move(draft));
}

void NetworkSettingsPresenter::DrawRestartConfirmation(const px::ui::Localizer& localizer) {
    if (lastOperation_ == NetworkOperation::SavedNeedsRestart) {
        px::ui::OpenModal({"RestartConfirmation"});
    }
    px::ui::ModalScope dialog{{"RestartConfirmation"}, 440.0F};
    if (dialog.Open()) {
        static_cast<void>(px::ui::DialogHeader({"network-restart-close"}, localizer.Text(px::ui::TextId::Restart),
                                               localizer.Text(px::ui::TextId::RestartRenderPrompt),
                                               {.icon = px::ui::VectorIcon::Refresh, .closeable = false}));
        const float buttonWidth{px::ui::Scale(140.0F)};
        px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
        if (px::ui::ActionButton({"network-restart-later"}, localizer.Text(px::ui::TextId::Later),
                                 {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
            port_->Acknowledge();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"network-restart-now"}, localizer.Text(px::ui::TextId::RestartNow), {.width = buttonWidth})) {
            port_->RestartRender();
            ImGui::CloseCurrentPopup();
        }
    }
}

void NetworkSettingsPresenter::Draw(const px::ui::Localizer& localizer) {
    Synchronize();
    const NetworkPageAction action{page_.Draw(localizer)};
    const auto& draft = page_.Draft();
    switch (action) {
        case NetworkPageAction::ConsoleAddressChanged:
            port_->ParseConsoleAddress(draft.consoleAddress);
        break;
    case NetworkPageAction::VerifyRequested:
            port_->Verify(draft.consoleAddress);
        break;
    case NetworkPageAction::SaveRequested:
            port_->Save(draft.consoleAddress);
        break;
    case NetworkPageAction::None:
        break;
    }
    DrawRestartConfirmation(localizer);
}

} // namespace px::panel::ui
