#include "settings_page.h"

#include "px_ui/components/navigation.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

#include <imgui.h>

#include <array>
#include <utility>

namespace px::panel::ui {

SettingsPage::SettingsPage(std::shared_ptr<NetworkSettingsPort> networkPort, std::shared_ptr<SettingsPort> settingsPort,
                           std::shared_ptr<ServerStatusPort> serverStatusPort, std::shared_ptr<SecurityRecordsPort> securityRecordsPort)
    : general_{settingsPort, std::move(serverStatusPort)}, network_{std::move(networkPort)}, security_{settingsPort, std::move(securityRecordsPort)},
      controller_{settingsPort}, about_{std::move(settingsPort)} {}

std::optional<px::ui::Theme> SettingsPage::Draw(px::ui::Localizer& localizer, px::ui::Theme& theme) {
    struct Section final {
        SettingsSection id{};
        px::ui::TextId label{};
    };
    constexpr std::array sections{
        Section{SettingsSection::General, px::ui::TextId::General},   Section{SettingsSection::Network, px::ui::TextId::Network},
        Section{SettingsSection::Security, px::ui::TextId::Security}, Section{SettingsSection::Controller, px::ui::TextId::Controller},
        Section{SettingsSection::About, px::ui::TextId::About},
    };
    const float tabWidth{px::ui::Scale(96.0F)};
    for (std::size_t index{}; index < sections.size(); ++index) {
        if (index != 0)
            ImGui::SameLine();
        const auto& section = sections[index];
        const std::string id{"settings-section-" + std::to_string(static_cast<int>(section.id))};
        if (px::ui::TabItem({id}, localizer.Text(section.label), section.id == selected_, tabWidth)) {
            selected_ = section.id;
        }
    }
    std::optional<px::ui::Theme> selectedTheme{};
    {
        px::ui::CardScope contentCard{{"SettingsContentCard"}, {0.0F, ImGui::GetContentRegionAvail().y}};
        if (contentCard.Visible()) {
            switch (selected_) {
            case SettingsSection::General:
                selectedTheme = general_.Draw(localizer, theme);
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
        }
    }
    return selectedTheme;
}

} // namespace px::panel::ui
