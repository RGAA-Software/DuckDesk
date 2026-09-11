#include "placeholder_page.h"

#include <imgui.h>

namespace px::panel::ui {
namespace {

px::ui::TextId PageTitle(const PanelPage page) {
    switch (page) {
    case PanelPage::RemoteControl:
        return px::ui::TextId::RemoteControl;
    case PanelPage::CloudApplications:
        return px::ui::TextId::CloudApplications;
    case PanelPage::ServerStatus:
        return px::ui::TextId::ServerStatus;
    case PanelPage::Security:
        return px::ui::TextId::Security;
    case PanelPage::Settings:
        return px::ui::TextId::Settings;
    case PanelPage::Hardware:
        return px::ui::TextId::Hardware;
    }
    return px::ui::TextId::Settings;
}

} // namespace

void DrawPlaceholderPage(const PanelPage page, const px::ui::Localizer& localizer) {
    ImGui::TextUnformatted(localizer.Text(PageTitle(page)).data());
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", localizer.Text(px::ui::TextId::MigrationPending).data());
}

} // namespace px::panel::ui
