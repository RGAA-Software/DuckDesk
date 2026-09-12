#include "remote_device_actions.h"

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
    if (ImGui::MenuItem(localizer.Text(px::ui::TextId::StartControl).data()))
        Start(device, false);
    if (ImGui::MenuItem(localizer.Text(px::ui::TextId::ViewOnly).data()))
        Start(device, true);
    ImGui::Separator();
    if (ImGui::MenuItem(localizer.Text(px::ui::TextId::EditDevice).data()))
        Edit(device);
    if (ImGui::MenuItem(localizer.Text(px::ui::TextId::FileTransfer).data()))
        FileTransfer(device);
    ImGui::Separator();
    if (ImGui::MenuItem(localizer.Text(px::ui::TextId::Delete).data()))
        Remove(device);
}

void RemoteDeviceActions::DrawPasswordDialog(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDevicePassword")};
    if (openPasswordDialog_) {
        ImGui::OpenPopup(popupId.c_str());
        openPasswordDialog_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal(popupId.c_str(), {}, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::Password).data());
    ImGui::SetNextItemWidth(px::ui::Scale(360.0F));
    ImGui::InputText("##remote-device-password", &pendingPassword_, ImGuiInputTextFlags_Password);
    if (ImGui::Button(localizer.Text(px::ui::TextId::Connect).data()) && !pendingPassword_.empty()) {
        port_->Connect(std::move(pendingTarget_), std::move(pendingPassword_), pendingViewOnly_);
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        pendingTarget_.clear();
        pendingPassword_.clear();
        pendingViewOnly_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RemoteDeviceActions::DrawEditor(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoteDeviceEditor")};
    if (openEditor_) {
        ImGui::OpenPopup(popupId.c_str());
        openEditor_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal(popupId.c_str(), {}, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!editingDevice_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& device = *editingDevice_;
    ImGui::InputText(localizer.Text(px::ui::TextId::DeviceName).data(), &device.name);
    ImGui::SeparatorText(localizer.Text(px::ui::TextId::DeviceSettings).data());
    ImGui::Checkbox(localizer.Text(px::ui::TextId::CaptureAudio).data(), &device.audio);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::EnableClipboard).data(), &device.clipboard);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ViewOnly).data(), &device.viewOnly);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::SplitWindows).data(), &device.splitWindows);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceSoftware).data(), &device.forceSoftware);
    if (ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceTcp).data(), &device.forceTcp) && device.forceTcp)
        device.forceRelay = false;
    if (ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceRelay).data(), &device.forceRelay) && device.forceRelay)
        device.forceTcp = false;
    ImGui::Checkbox(localizer.Text(px::ui::TextId::WaitForDebugger).data(), &device.waitForDebugger);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::ForceGdiCapture).data(), &device.forceGdiCapture);
    ImGui::Checkbox(localizer.Text(px::ui::TextId::DisableVulkan).data(), &device.disableVulkan);
    const bool valid{!device.name.empty()};
    if (!valid)
        ImGui::TextColored(ImVec4{0.90F, 0.22F, 0.28F, 1.0F}, "%s", localizer.Text(px::ui::TextId::InvalidDeviceSettings).data());
    if (!valid)
        ImGui::BeginDisabled();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Save).data())) {
        port_->SaveDevice(std::move(device));
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    if (!valid)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        editingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RemoteDeviceActions::DrawRemoveConfirmation(const px::ui::Localizer& localizer) {
    const std::string popupId{PopupId("RemoveRemoteDevice")};
    if (openRemoveConfirmation_) {
        ImGui::OpenPopup(popupId.c_str());
        openRemoveConfirmation_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5F, 0.5F});
    if (!ImGui::BeginPopupModal(popupId.c_str(), {}, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(localizer.Text(px::ui::TextId::RemoveDevicePrompt).data());
    if (ImGui::Button(localizer.Text(px::ui::TextId::Delete).data()) && removingDevice_) {
        port_->DeleteDevice(removingDevice_->streamId);
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Cancel).data())) {
        removingDevice_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RemoteDeviceActions::DrawDialogs(const px::ui::Localizer& localizer) {
    DrawPasswordDialog(localizer);
    DrawEditor(localizer);
    DrawRemoveConfirmation(localizer);
}

} // namespace px::panel::ui
