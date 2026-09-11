#include "network_settings_page.h"

#include "px_ui/layout_metrics.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <string>
#include <string_view>
#include <utility>

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
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    DrawDisabledText(label);
    ImGui::TableNextColumn();
    DrawText(value);
    ImGui::TableNextColumn();
    DrawDisabledText(purpose);
}

void BeginEndpointTable(const std::string_view identifier) {
    constexpr ImGuiTableFlags flags{ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH};
    ImGui::BeginTable(identifier.data(), 3, flags);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.95F);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.70F);
    ImGui::TableSetupColumn("purpose", ImGuiTableColumnFlags_WidthStretch, 1.85F);
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
    ImGui::InputTextMultiline("##authorization", &draft_.authorizationInfo, ImVec2{-1.0F, px::ui::Scale(92.0F)});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        return NetworkPageAction::AuthorizationChanged;
    }

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::ResolvedControlEndpoints));
    ImGui::BeginChild("ResolvedEndpoints", ImVec2{0.0F, px::ui::Scale(108.0F)}, ImGuiChildFlags_Borders);
    BeginEndpointTable("ResolvedEndpointTable");
    DrawEndpoint(text(px::ui::TextId::Supervisor), DisplayPort(draft_.consolePort), text(px::ui::TextId::NodeManagement));
    DrawEndpoint(text(px::ui::TextId::Relay), DisplayPort(draft_.relayPort), text(px::ui::TextId::ReliableRoutedConnection));
    ImGui::EndTable();
    ImGui::EndChild();

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodePublicAddress));
    DrawDisabledText(text(px::ui::TextId::OptionalPublicAddress));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##publicAddress", text(px::ui::TextId::PublicAddressHint).data(), &draft_.nodePublicAddress);

    ImGui::Spacing();
    DrawText(text(px::ui::TextId::NodeListeningPorts));
    ImGui::BeginChild("NodePorts", ImVec2{0.0F, px::ui::Scale(172.0F)}, ImGuiChildFlags_Borders);
    BeginEndpointTable("NodePortTable");
    DrawEndpoint(text(px::ui::TextId::ServiceManagementPort), std::to_string(draft_.serviceManagementPort),
                 text(px::ui::TextId::ServiceManagementPurpose));
    DrawEndpoint(text(px::ui::TextId::DesktopConnectionPort), std::to_string(draft_.desktopConnectionPort),
                 text(px::ui::TextId::DesktopConnectionPurpose));
    DrawEndpoint(text(px::ui::TextId::ApplicationPortPool), DisplayRange(draft_.applicationPorts), text(px::ui::TextId::ApplicationPortPurpose));
    DrawEndpoint(text(px::ui::TextId::RtcMediaPool), DisplayRange(draft_.rtcPorts), text(px::ui::TextId::RtcPortPurpose));
    DrawEndpoint(text(px::ui::TextId::PanelListeningPort), std::to_string(draft_.panelListeningPort), text(px::ui::TextId::PanelListeningPurpose));
    ImGui::EndTable();
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(text(px::ui::TextId::Save).data(), px::ui::Scale(ImVec2{150.0F, 40.0F}))) {
        return NetworkPageAction::SaveRequested;
    }
    ImGui::SameLine();
    if (ImGui::Button(text(px::ui::TextId::Verify).data(), px::ui::Scale(ImVec2{150.0F, 40.0F}))) {
        return NetworkPageAction::VerifyRequested;
    }
    ImGui::SameLine();
    DrawDisabledText(text(status_));
    return NetworkPageAction::None;
}

const NetworkSettingsDraft& NetworkSettingsPage::Draft() const noexcept {
    return draft_;
}

void NetworkSettingsPage::SetDraft(NetworkSettingsDraft draft) {
    draft_ = std::move(draft);
}

void NetworkSettingsPage::SetStatus(const px::ui::TextId status) noexcept {
    status_ = status;
}

} // namespace px::panel::ui
