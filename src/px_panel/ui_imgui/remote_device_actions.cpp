#include "remote_device_actions.h"

#include "px_ui/components/button.h"
#include "px_ui/components/form.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <utility>

namespace px::panel::ui {

RemoteDeviceActions::RemoteDeviceActions(std::shared_ptr<RemoteControlPort> port, std::string idScope)
    : port_{std::move(port)}, idScope_{std::move(idScope)} {}

std::string RemoteDeviceActions::PopupId(const std::string_view name) const {
    return std::string{name} + "##" + idScope_;
}

void RemoteDeviceActions::Start(const RemoteDeviceCard& device, const bool viewOnly) {
    const std::string target{device.deviceId.empty() ? device.host : device.deviceId};
    if (port_->RequiresPassword(target)) {
        pendingTarget_ = target;
        pendingPassword_.clear();
        pendingViewOnly_ = viewOnly;
        openPasswordDialog_ = true;
        return;
    }
    port_->StartStream(device.streamId, viewOnly);
}

void RemoteDeviceActions::Edit(const RemoteDeviceCard& device) {
    editingDevice_ = device;
    openEditor_ = true;
}

void RemoteDeviceActions::FileTransfer(const RemoteDeviceCard& device) {
    port_->StartFileTransfer(device.streamId);
}

void RemoteDeviceActions::Remove(const RemoteDeviceCard& device) {
    removingDevice_ = device;
    openRemoveConfirmation_ = true;
}

void RemoteDeviceActions::DrawContextMenu(const RemoteDeviceCard& device, const px::ui::Localizer& localizer) {
    if (px::ui::MenuAction({"device-start"}, localizer.Text(px::ui::TextId::StartControl)))
        Start(device, false);
    if (px::ui::MenuAction({"device-view"}, localizer.Text(px::ui::TextId::ViewOnly)))
        Start(device, true);
    ImGui::Separator();
    if (px::ui::MenuAction({"device-edit"}, localizer.Text(px::ui::TextId::EditDevice)))
        Edit(device);
    if (px::ui::MenuAction({"device-files"}, localizer.Text(px::ui::TextId::FileTransfer)))
        FileTransfer(device);
    ImGui::Separator();
    if (px::ui::MenuAction({"device-delete"}, localizer.Text(px::ui::TextId::Delete)))
        Remove(device);
}

void RemoteDeviceActions::DrawPasswordDialog(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDevicePassword")};
    if (openPasswordDialog_) {
        px::ui::OpenModal({popupId});
        openPasswordDialog_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 420.0F};
    if (!dialog.Open())
        return;
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::Password));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::Password));
    static_cast<void>(px::ui::TextField({"remote-device-password"}, pendingPassword_, {}, {}, ImGuiInputTextFlags_Password));
    if (px::ui::ActionButton({"remote-device-connect"}, localizer.Text(px::ui::TextId::Connect), {.icon = px::ui::VectorIcon::Connect}) &&
        !pendingPassword_.empty()) {
        port_->Connect(std::move(pendingTarget_), std::move(pendingPassword_), pendingViewOnly_);
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"remote-device-cancel"}, localizer.Text(px::ui::TextId::Cancel), {.variant = px::ui::ButtonVariant::Outline})) {
        pendingTarget_.clear();
        pendingPassword_.clear();
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawEditor(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDeviceEditor")};
    if (openEditor_) {
        px::ui::OpenModal({popupId});
        openEditor_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 520.0F};
    if (!dialog.Open())
        return;
    if (!editingDevice_) {
        ImGui::CloseCurrentPopup();
        return;
    }
    auto& device = *editingDevice_;
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::DeviceSettings));
    px::ui::FieldLabel(localizer.Text(px::ui::TextId::DeviceName));
    static_cast<void>(px::ui::TextField({"remote-device-name"}, device.name));
    px::ui::HorizontalSeparator();
    static_cast<void>(px::ui::CheckboxField({"device-audio"}, localizer.Text(px::ui::TextId::CaptureAudio), device.audio));
    static_cast<void>(px::ui::CheckboxField({"device-clipboard"}, localizer.Text(px::ui::TextId::EnableClipboard), device.clipboard));
    static_cast<void>(px::ui::CheckboxField({"device-view-only"}, localizer.Text(px::ui::TextId::ViewOnly), device.viewOnly));
    static_cast<void>(px::ui::CheckboxField({"device-split"}, localizer.Text(px::ui::TextId::SplitWindows), device.splitWindows));
    static_cast<void>(px::ui::CheckboxField({"device-software"}, localizer.Text(px::ui::TextId::ForceSoftware), device.forceSoftware));
    if (px::ui::CheckboxField({"device-tcp"}, localizer.Text(px::ui::TextId::ForceTcp), device.forceTcp) && device.forceTcp)
        device.forceRelay = false;
    if (px::ui::CheckboxField({"device-relay"}, localizer.Text(px::ui::TextId::ForceRelay), device.forceRelay) && device.forceRelay)
        device.forceTcp = false;
    static_cast<void>(px::ui::CheckboxField({"device-debugger"}, localizer.Text(px::ui::TextId::WaitForDebugger), device.waitForDebugger));
    static_cast<void>(px::ui::CheckboxField({"device-gdi"}, localizer.Text(px::ui::TextId::ForceGdiCapture), device.forceGdiCapture));
    static_cast<void>(px::ui::CheckboxField({"device-vulkan"}, localizer.Text(px::ui::TextId::DisableVulkan), device.disableVulkan));
    const bool valid{!device.name.empty()};
    if (!valid)
        px::ui::FieldError(localizer.Text(px::ui::TextId::InvalidDeviceSettings));
    if (px::ui::ActionButton({"device-save"}, localizer.Text(px::ui::TextId::Save), {.disabled = !valid})) {
        port_->SaveDevice(std::move(device));
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-edit-cancel"}, localizer.Text(px::ui::TextId::Cancel), {.variant = px::ui::ButtonVariant::Outline})) {
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawRemoveConfirmation(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoveRemoteDevice")};
    if (openRemoveConfirmation_) {
        px::ui::OpenModal({popupId});
        openRemoveConfirmation_ = false;
    }
    px::ui::ModalScope dialog{{popupId}, 420.0F};
    if (!dialog.Open())
        return;
    px::ui::SectionTitle(localizer.Text(px::ui::TextId::Delete));
    ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::RemoveDevicePrompt).data());
    if (px::ui::ActionButton({"device-remove"}, localizer.Text(px::ui::TextId::Delete), {.variant = px::ui::ButtonVariant::Destructive}) &&
        removingDevice_) {
        port_->DeleteDevice(removingDevice_->streamId);
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"device-remove-cancel"}, localizer.Text(px::ui::TextId::Cancel), {.variant = px::ui::ButtonVariant::Outline})) {
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
}

void RemoteDeviceActions::DrawDialogs(const px::ui::Localizer& localizer) {
    DrawPasswordDialog(localizer);
    DrawEditor(localizer);
    DrawRemoveConfirmation(localizer);
}

} // namespace px::panel::ui
