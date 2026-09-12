#include "settings_page.h"

#include <imgui.h>

#include <array>
#include <utility>

namespace px::panel::ui {

SettingsPage::SettingsPage(std::shared_ptr<NetworkSettingsPort> networkPort, std::shared_ptr<SettingsPort> settingsPort)
    : general_{settingsPort}, network_{std::move(networkPort)}, security_{settingsPort}, controller_{settingsPort}, about_{std::move(settingsPort)} {}

void SettingsPage::Draw(const px::ui::Localizer& localizer) {
    struct Section final {
        SettingsSection id{};
        px::ui::TextId label{};
    };
    constexpr std::array sections{
        Section{SettingsSection::General, px::ui::TextId::General}, Section{SettingsSection::Network, px::ui::TextId::Network},
        Section{SettingsSection::Security, px::ui::TextId::Security}, Section{SettingsSection::Controller, px::ui::TextId::Controller},
        Section{SettingsSection::About, px::ui::TextId::About},
    };
    ImGui::BeginChild("SettingsSections", ImVec2{160.0F, 0.0F}, ImGuiChildFlags_Borders);
    for (const auto& section : sections) {
        if (ImGui::Selectable(localizer.Text(section.label).data(), section.id == selected_)) {
            selected_ = section.id;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("SettingsContent", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (selected_) {
    case SettingsSection::General:
        general_.Draw(localizer);
        break;
    case SettingsSection::Network:
        network_.Draw(localizer);
        break;
    case SettingsSection::Security:
        security_.Draw(localizer);
        break;
    case SettingsSection::Controller:
        controller_.Draw(localizer);
        break;
    case SettingsSection::About:
        about_.Draw(localizer);
        break;
    }
    ImGui::EndChild();
}

} // namespace px::panel::ui
