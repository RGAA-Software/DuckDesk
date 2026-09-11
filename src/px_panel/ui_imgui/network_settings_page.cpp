#include "network_settings_page.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <string>
#include <string_view>

namespace px::panel::ui {
namespace {

void DrawText(const std::string_view text) {
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    DrawText(text);
    ImGui::PopStyleColor();
}

void DrawEndpoint(const std::string_view label, const std::string_view value, const std::string_view purpose) {
    DrawDisabledText(label);
    ImGui::SameLine(190.0F);
    DrawText(value);
    ImGui::SameLine(430.0F);
    DrawDisabledText(purpose);
}

std::string DisplayPort(const std::optional<int> port) {
    return port.has_value() && *port > 0 ? std::to_string(*port) : "--";
}

std::string DisplayRange(const PortRange range) {
    return std::to_string(range.first) + "-" + std::to_string(range.last);
}

} // namespace

NetworkPageAction NetworkSettingsPage::Draw(const px::ui::Localizer& localizer) {
    const auto text = [&localizer](const px::ui::TextId id) { return localizer.Text(id); };
    DrawText(text(px::ui::TextId::SettingsNetwork));
    DrawDisabledText(text(px::ui::TextId::ConnectionAddresses));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawText(text(px::ui::TextId::Authorization));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextMultiline("##authorization", &draft_.authorizationInfo, ImVec2{-1.0F, 92.0F});

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::ResolvedControlEndpoints));
    ImGui::BeginChild("ResolvedEndpoints", ImVec2{0.0F, 108.0F}, ImGuiChildFlags_Borders);
    DrawEndpoint(text(px::ui::TextId::Supervisor), DisplayPort(draft_.consolePort), text(px::ui::TextId::NodeManagement));
    DrawEndpoint(text(px::ui::TextId::Relay), DisplayPort(draft_.relayPort), text(px::ui::TextId::ReliableRoutedConnection));
    ImGui::EndChild();

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodePublicAddress));
    DrawDisabledText(text(px::ui::TextId::OptionalPublicAddress));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##publicAddress", text(px::ui::TextId::PublicAddressHint).data(), &draft_.nodePublicAddress);

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodeListeningPorts));
    ImGui::BeginChild("NodePorts", ImVec2{0.0F, 138.0F}, ImGuiChildFlags_Borders);
    DrawEndpoint(text(px::ui::TextId::ServiceManagementPort), std::to_string(draft_.serviceManagementPort),
                 text(px::ui::TextId::ServiceManagementPurpose));
    DrawEndpoint(text(px::ui::TextId::DesktopConnectionPort), std::to_string(draft_.desktopConnectionPort),
                 text(px::ui::TextId::DesktopConnectionPurpose));
    DrawEndpoint(text(px::ui::TextId::ApplicationPortPool), DisplayRange(draft_.applicationPorts), text(px::ui::TextId::ApplicationPortPurpose));
    DrawEndpoint(text(px::ui::TextId::RtcMediaPool), DisplayRange(draft_.rtcPorts), text(px::ui::TextId::RtcPortPurpose));
    DrawEndpoint(text(px::ui::TextId::PanelListeningPort), std::to_string(draft_.panelListeningPort), text(px::ui::TextId::PanelListeningPurpose));
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(text(px::ui::TextId::Save).data(), ImVec2{150.0F, 40.0F})) {
        return NetworkPageAction::SaveRequested;
    }
    ImGui::SameLine();
    DrawDisabledText(text(status_));
    return NetworkPageAction::None;
}

const NetworkSettingsDraft& NetworkSettingsPage::Draft() const noexcept {
    return draft_;
}

void NetworkSettingsPage::SetStatus(const px::ui::TextId status) noexcept {
    status_ = status;
}

} // namespace px::panel::ui
