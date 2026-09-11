#include "network_settings_presenter.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

NetworkSettingsPresenter::NetworkSettingsPresenter(std::shared_ptr<NetworkSettingsPort> port) : port_{std::move(port)} {
    Synchronize();
}

px::ui::TextId NetworkSettingsPresenter::StatusText(const NetworkOperation operation) const noexcept {
    switch (operation) {
    case NetworkOperation::InvalidAuthorization:
        return px::ui::TextId::InvalidAuthorization;
    case NetworkOperation::InvalidPublicAddress:
        return px::ui::TextId::InvalidPublicAddress;
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
    draft.relayPort = state.settings.relayPort;
    draft.serviceManagementPort = state.settings.serviceManagementPort;
    draft.desktopConnectionPort = state.settings.desktopConnectionPort;
    draft.applicationPorts = state.settings.applicationPorts;
    draft.rtcPorts = state.settings.rtcPorts;
    draft.panelListeningPort = state.settings.panelListeningPort;
    if (draft.authorizationInfo.empty() && draft.nodePublicAddress.empty()) {
        draft.authorizationInfo = state.settings.authorizationInfo;
        draft.nodePublicAddress = state.settings.nodePublicAddress;
    }
    page_.SetDraft(std::move(draft));
}

void NetworkSettingsPresenter::DrawRestartConfirmation(const px::ui::Localizer& localizer) {
    if (lastOperation_ == NetworkOperation::SavedNeedsRestart) {
        ImGui::OpenPopup("RestartConfirmation");
    }
    if (ImGui::BeginPopupModal("RestartConfirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(localizer.Text(px::ui::TextId::RestartRenderPrompt).data());
        if (ImGui::Button(localizer.Text(px::ui::TextId::RestartNow).data(), px::ui::Scale(ImVec2{140.0F, 36.0F}))) {
            port_->RestartRender();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(localizer.Text(px::ui::TextId::Later).data(), px::ui::Scale(ImVec2{140.0F, 36.0F}))) {
            port_->Acknowledge();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void NetworkSettingsPresenter::Draw(const px::ui::Localizer& localizer) {
    Synchronize();
    const NetworkPageAction action{page_.Draw(localizer)};
    const auto& draft = page_.Draft();
    switch (action) {
    case NetworkPageAction::AuthorizationChanged:
        port_->ParseAuthorization(draft.authorizationInfo);
        break;
    case NetworkPageAction::VerifyRequested:
        port_->Verify(draft.authorizationInfo);
        break;
    case NetworkPageAction::SaveRequested:
        port_->Save(draft.authorizationInfo, draft.nodePublicAddress);
        break;
    case NetworkPageAction::None:
        break;
    }
    DrawRestartConfirmation(localizer);
}

} // namespace px::panel::ui
