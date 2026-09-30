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
    case NetworkOperation::Saved:
        return px::ui::TextId::Saved;
    case NetworkOperation::Failed:
        return px::ui::TextId::OperationFailed;
    case NetworkOperation::Idle:
        return px::ui::TextId::NetworkIdleStatus;
    }
    return px::ui::TextId::OperationFailed;
}

void NetworkSettingsPresenter::Synchronize() {
    const auto state = port_->Snapshot();
    const bool operationChanged = state.operation != lastOperation_;
    if (operationChanged) {
        page_.SetStatus(StatusText(state.operation));
        lastOperation_ = state.operation;
    }
    auto draft = page_.Draft();
    draft.consolePort = state.settings.consolePort;
    draft.officialConsoleAvailable = state.settings.officialConsoleAvailable;
    draft.serviceManagementPort = state.settings.serviceManagementPort;
    draft.desktopConnectionPort = state.settings.desktopConnectionPort;
    draft.applicationPorts = state.settings.applicationPorts;
    draft.rtcPorts = state.settings.rtcPorts;
    draft.panelListeningPort = state.settings.panelListeningPort;
    if (draft.consoleAddress.empty() || (operationChanged && state.operation == NetworkOperation::Saved)) {
        draft.consoleAddress = state.settings.consoleAddress;
    }
    page_.SetDraft(std::move(draft));
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
    case NetworkPageAction::UseOfficialRequested:
            port_->UseOfficial();
        break;
    case NetworkPageAction::None:
        break;
    }
}

} // namespace px::panel::ui
