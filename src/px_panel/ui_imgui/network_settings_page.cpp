#include "network_settings_page.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "px_ui/components/button.h"
#include "px_ui/components/data_view.h"
#include "px_ui/components/form.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

namespace px::panel::ui {
namespace {

void DrawText(const std::string_view text) { ImGui::TextUnformatted(text.data(), text.data() + text.size()); }

void DrawDisabledText(const std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    DrawText(text);
    ImGui::PopStyleColor();
}

void DrawEndpoint(const std::string_view label, const std::string_view value, const std::string_view purpose) {
    const float rowHeight{px::ui::Scale(32.0F)};
    const float contentHeight{rowHeight - ImGui::GetStyle().CellPadding.y * 2.0F};
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableNextColumn();
    {
        const px::ui::ScopedStyleColor labelColor{ImGuiCol_Text, px::ui::CurrentThemeTokens().mutedForeground};
        px::ui::ClippedText(label, 0.0F, contentHeight);
    }
    ImGui::TableNextColumn();
    px::ui::ClippedText(value, 0.0F, contentHeight);
    ImGui::TableNextColumn();
    px::ui::ClippedText(purpose, 0.0F, contentHeight);
}

bool BeginEndpointTable(const std::string_view identifier) {
    constexpr ImGuiTableFlags flags{ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH};
    if (!ImGui::BeginTable(identifier.data(), 3, flags)) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.95F);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.70F);
    ImGui::TableSetupColumn("purpose", ImGuiTableColumnFlags_WidthStretch, 1.85F);
    return true;
}

std::string DisplayPort(const std::optional<int> port) { return port.has_value() && *port > 0 ? std::to_string(*port) : "--"; }

std::string DisplayRange(const PortRange range) { return std::to_string(range.first) + "-" + std::to_string(range.last); }

}  // namespace

NetworkPageAction NetworkSettingsPage::Draw(const px::ui::Localizer& localizer) {
    const auto text = [&localizer](const px::ui::TextId id) { return localizer.Text(id); };
    px::ui::SectionTitle(text(px::ui::TextId::SettingsNetwork));
    px::ui::FieldDescription(text(px::ui::TextId::ConnectionAddresses));
    px::ui::HorizontalSeparator();
    ImGui::Spacing();

    px::ui::FieldLabel(text(px::ui::TextId::ConsoleAddress));
    px::ui::FieldDescription(text(px::ui::TextId::ConsoleAddressHint));
    if (draft_.officialConsoleAvailable && px::ui::ActionButton({"network-use-official"}, text(px::ui::TextId::UseOfficialConsole),
                                                                {.variant = px::ui::ButtonVariant::Outline, .width = px::ui::Scale(190.0F)})) {
        return NetworkPageAction::UseOfficialRequested;
    }
    const float verifyButtonWidth{px::ui::Scale(150.0F)};
    const float addressWidth{std::max(px::ui::Scale(160.0F), ImGui::GetContentRegionAvail().x - verifyButtonWidth - ImGui::GetStyle().ItemSpacing.x)};
    static_cast<void>(px::ui::TextField({"console-address"}, draft_.consoleAddress, "https://console.example.com",
                                        {.width = addressWidth, .readOnly = !draft_.consoleAddressEditable}));
    const bool addressChanged{ImGui::IsItemDeactivatedAfterEdit()};
    ImGui::SameLine();
    if (px::ui::ActionButton({"network-verify"}, text(px::ui::TextId::Verify),
                             {.variant = px::ui::ButtonVariant::Outline, .width = verifyButtonWidth})) {
        return NetworkPageAction::VerifyRequested;
    }
    if (addressChanged) return NetworkPageAction::ConsoleAddressChanged;

    ImGui::Spacing();
    px::ui::SectionTitle(text(px::ui::TextId::ResolvedConsoleEndpoint));
    {
        px::ui::CardScope resolved{{"ResolvedEndpoints"}, {0.0F, px::ui::Scale(104.0F)}};
        if (resolved.Visible() && BeginEndpointTable("ResolvedEndpointTable")) {
            DrawEndpoint(text(px::ui::TextId::ConsoleService), DisplayPort(draft_.consolePort), text(px::ui::TextId::ConsoleApiPurpose));
            DrawEndpoint(text(px::ui::TextId::TransportSecurity), "HTTPS", text(px::ui::TextId::HttpsRequired));
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    px::ui::SectionTitle(text(px::ui::TextId::NodeListeningPorts));
    {
        px::ui::CardScope ports{{"NodePorts"}, {0.0F, px::ui::Scale(200.0F)}};
        if (ports.Visible() && BeginEndpointTable("NodePortTable")) {
            DrawEndpoint(text(px::ui::TextId::ServiceManagementPort), std::to_string(draft_.serviceManagementPort),
                         text(px::ui::TextId::ServiceManagementPurpose));
            DrawEndpoint(text(px::ui::TextId::DesktopConnectionPort), std::to_string(draft_.desktopConnectionPort),
                         text(px::ui::TextId::DesktopConnectionPurpose));
            DrawEndpoint(text(px::ui::TextId::ApplicationPortPool), DisplayRange(draft_.applicationPorts),
                         text(px::ui::TextId::ApplicationPortPurpose));
            DrawEndpoint(text(px::ui::TextId::RtcMediaPool), DisplayRange(draft_.rtcPorts), text(px::ui::TextId::RtcPortPurpose));
            DrawEndpoint(text(px::ui::TextId::PanelListeningPort), std::to_string(draft_.panelListeningPort),
                         text(px::ui::TextId::PanelListeningPurpose));
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (px::ui::ActionButton({"network-save"}, text(px::ui::TextId::Save),
                             {.width = px::ui::Scale(150.0F), .disabled = !draft_.consoleAddressEditable})) {
        return NetworkPageAction::SaveRequested;
    }
    ImGui::SameLine();
    DrawDisabledText(text(status_));
    return NetworkPageAction::None;
}

const NetworkSettingsDraft& NetworkSettingsPage::Draft() const noexcept { return draft_; }

void NetworkSettingsPage::SetDraft(NetworkSettingsDraft draft) { draft_ = std::move(draft); }

void NetworkSettingsPage::SetStatus(const px::ui::TextId status) noexcept { status_ = status; }

}  // namespace px::panel::ui
