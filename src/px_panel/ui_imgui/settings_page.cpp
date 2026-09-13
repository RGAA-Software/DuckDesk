#include "settings_page.h"
#include "panel_layout.h"

#include "px_ui/components/button.h"
#include "px_ui/components/navigation.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"

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
        Section{SettingsSection::General, px::ui::TextId::General},   Section{SettingsSection::Network, px::ui::TextId::Network},
        Section{SettingsSection::Security, px::ui::TextId::Security}, Section{SettingsSection::Controller, px::ui::TextId::Controller},
        Section{SettingsSection::About, px::ui::TextId::About},
    };
    const float cardHeight{ImGui::GetContentRegionAvail().y};
    {
        px::ui::CardScope sectionsCard{
            {"SettingsSectionsCard"}, {px::ui::Scale(118.0F), cardHeight}, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse};
        if (sectionsCard.Visible()) {
            for (const auto& section : sections) {
                const std::string id{"settings-section-" + std::to_string(static_cast<int>(section.id))};
                if (px::ui::ActionButton({id}, localizer.Text(section.label),
                                         {.variant = section.id == selected_ ? px::ui::ButtonVariant::Accent : px::ui::ButtonVariant::Ghost,
                                          .size = px::ui::WidgetSize::Xs,
                                          .width = ImGui::GetContentRegionAvail().x})) {
                    selected_ = section.id;
                }
            }
        }
    }
    ImGui::SameLine(0.0F, layout::CardGap());
    {
        px::ui::CardScope contentCard{{"SettingsContentCard"}, {0.0F, cardHeight}};
        if (contentCard.Visible()) {
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
        }
    }
}

} // namespace px::panel::ui
