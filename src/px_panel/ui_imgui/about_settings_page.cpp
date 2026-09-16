#include "about_settings_page.h"

#include "px_ui/components/button.h"
#include "px_ui/components/surface.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <utility>

namespace px::panel::ui {

AboutSettingsPage::AboutSettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void AboutSettingsPage::Draw(const px::ui::Localizer& localizer) const {
    px::ui::SectionTitle("Pixels");
    px::ui::MutedText(std::string{localizer.Text(px::ui::TextId::Version)} + " " + port_->Snapshot().version);
    px::ui::HorizontalSeparator();
    ImGui::Spacing();
    ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::AboutDescription).data());
    if (px::ui::ActionButton({"about-updates"}, localizer.Text(px::ui::TextId::CheckForUpdates))) {
        port_->CheckForUpdates();
    }
    ImGui::SameLine();
    if (px::ui::ActionButton({"about-website"}, localizer.Text(px::ui::TextId::Website), {.variant = px::ui::ButtonVariant::Outline})) {
        SDL_OpenURL("https://pixels.yun");
    }
}

} // namespace px::panel::ui
