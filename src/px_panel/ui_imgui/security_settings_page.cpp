#include "security_settings_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <utility>

namespace px::panel::ui {

SecuritySettingsPage::SecuritySettingsPage(std::shared_ptr<SettingsPort> port, std::shared_ptr<SecurityRecordsPort> securityRecordsPort)
    : port_{std::move(port)}, records_{std::move(securityRecordsPort)} {}

void SecuritySettingsPage::Draw(const px::ui::Localizer& localizer) {
    if (!loaded_) {
        const auto state = port_->Snapshot();
        disconnectAutoLock_ = state.disconnectAutoLock;
        logDestination_ = state.logDestination;
        loaded_ = true;
    }
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::SecuritySettings));
    px::ui::HorizontalSeparator();
    if (px::ui::ActionButton({"security-access-records"}, localizer.Text(px::ui::TextId::VisitHistory),
                             {.variant = px::ui::ButtonVariant::Outline})) {
        openAccessRecords_ = true;
    }
    ImGui::Spacing();
    if (px::ui::ToggleSwitch({"security-auto-lock"}, localizer.Text(px::ui::TextId::DisconnectAutoLock), disconnectAutoLock_)) {
        port_->SetDisconnectAutoLock(disconnectAutoLock_);
    }
    const float fieldWidth{px::ui::Scale(420.0F)};
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::LongTermPassword));
    static_cast<void>(px::ui::PasswordField({"security-password"}, password_, {}, {.width = fieldWidth, .invalid = passwordRejected_}));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::ConfirmPassword));
    static_cast<void>(px::ui::PasswordField({"security-confirmation"}, confirmation_, {}, {.width = fieldWidth, .invalid = passwordRejected_}));
    if (px::ui::ActionButton({"security-set-password"}, localizer.Text(px::ui::TextId::SetPassword))) {
        passwordRejected_ = !port_->SetSecurityPassword(password_, confirmation_);
        if (!passwordRejected_) {
            password_.clear();
            confirmation_.clear();
        }
    }
    if (passwordRejected_) {
        ImGui::SameLine();
        px::ui::FieldError(localizer.Text(px::ui::TextId::PasswordInvalid));
    }
    const auto passwordState = port_->Snapshot().passwordUpdate;
    if (passwordState == PasswordUpdateState::Updating) {
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::UpdatingPassword), px::ui::BadgeVariant::Secondary);
    } else if (passwordState == PasswordUpdateState::Updated) {
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::PasswordUpdated), px::ui::BadgeVariant::Success);
    } else if (passwordState == PasswordUpdateState::RemoteFailed) {
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::PasswordRemoteFailed), px::ui::BadgeVariant::Warning);
    }
    ImGui::Spacing();
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::MaintenanceTools));
    px::ui::HorizontalSeparator();
    if (px::ui::ActionButton({"security-clear-data"}, localizer.Text(px::ui::TextId::ClearData), {.variant = px::ui::ButtonVariant::Destructive})) {
        confirmClear_ = true;
    }
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::LogDestination));
    static_cast<void>(px::ui::TextField({"log-destination"}, logDestination_));
    const auto logState = port_->Snapshot().logCollection;
    if (px::ui::ActionButton({"collect-logs"}, localizer.Text(px::ui::TextId::CollectLogs), {.busy = logState == LogCollectionState::Collecting})) {
        port_->CollectLogs(logDestination_);
    }
    if (logState == LogCollectionState::Collecting) {
        ImGui::SameLine();
        px::ui::MutedText(localizer.Text(px::ui::TextId::Collecting));
    } else if (logState == LogCollectionState::Completed) {
        ImGui::SameLine();
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::LogCollectionCompleted), px::ui::BadgeVariant::Success);
    } else if (logState == LogCollectionState::Failed) {
        ImGui::SameLine();
        px::ui::StatusBadge(localizer.Text(px::ui::TextId::LogCollectionFailed), px::ui::BadgeVariant::Destructive);
    }
    if (openAccessRecords_) {
        px::ui::OpenModal({"AccessRecordsDialog"});
        openAccessRecords_ = false;
    }
    {
        ImGui::SetNextWindowSize({px::ui::Scale(840.0F), px::ui::Scale(560.0F)}, ImGuiCond_Appearing);
        px::ui::ModalScope recordsDialog{{"AccessRecordsDialog"}, 840.0F, ImGuiWindowFlags_None};
        if (recordsDialog.Open()) {
            static_cast<void>(px::ui::DialogHeader({"access-records-close"}, localizer.Text(px::ui::TextId::VisitHistory)));
            ImGui::BeginChild("AccessRecordsContent", {0.0F, px::ui::Scale(440.0F)}, ImGuiChildFlags_None);
            records_.DrawEmbedded(localizer);
            ImGui::EndChild();
        }
    }
    if (confirmClear_) {
        px::ui::OpenModal({"ConfirmClearPanelData"});
        confirmClear_ = false;
    }
    px::ui::ModalScope dialog{{"ConfirmClearPanelData"}, 440.0F};
    if (dialog.Open()) {
        static_cast<void>(px::ui::DialogHeader(
            {"clear-panel-data-close"}, localizer.Text(px::ui::TextId::ClearData), localizer.Text(px::ui::TextId::ClearDataPrompt),
            {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Destructive, .closeable = false}));
        const float buttonWidth{px::ui::Scale(104.0F)};
        px::ui::DialogFooter(buttonWidth * 2.0F + ImGui::GetStyle().ItemSpacing.x);
        if (px::ui::ActionButton({"clear-panel-data-cancel"}, localizer.Text(px::ui::TextId::Cancel),
                                 {.variant = px::ui::ButtonVariant::Outline, .width = buttonWidth})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (px::ui::ActionButton({"clear-panel-data"}, localizer.Text(px::ui::TextId::Clear),
                                 {.variant = px::ui::ButtonVariant::Destructive, .width = buttonWidth})) {
            port_->ClearData();
            ImGui::CloseCurrentPopup();
        }
    }
}

} // namespace px::panel::ui
