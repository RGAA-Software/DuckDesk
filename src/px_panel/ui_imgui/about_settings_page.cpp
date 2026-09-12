#include "about_settings_page.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <utility>

namespace px::panel::ui {

AboutSettingsPage::AboutSettingsPage(std::shared_ptr<SettingsPort> port) : port_{std::move(port)} {}

void AboutSettingsPage::Draw(const px::ui::Localizer& localizer) const {
    ImGui::TextUnformatted("Pixels");
    ImGui::TextDisabled("%s %s", localizer.Text(px::ui::TextId::Version).data(), port_->Snapshot().version.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", localizer.Text(px::ui::TextId::AboutDescription).data());
    if (ImGui::Button(localizer.Text(px::ui::TextId::CheckForUpdates).data())) {
        port_->CheckForUpdates();
    }
    ImGui::SameLine();
    if (ImGui::Button("GitHub")) {
        SDL_OpenURL("https://github.com/RGAA-Software/GammaRay");
    }
    ImGui::SameLine();
    if (ImGui::Button(localizer.Text(px::ui::TextId::Website).data())) {
        SDL_OpenURL("https://pixels.yun");
    }
}

} // namespace px::panel::ui
