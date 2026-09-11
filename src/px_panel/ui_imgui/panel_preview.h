#pragma once

#include "network_settings_page.h"
#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

namespace px::panel::ui {

class PanelPreview final {
  public:
    void Draw();

  private:
    void DrawNavigation();
    void DrawNetworkPage();

    px::ui::Localizer localizer_{};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    NetworkSettingsPage networkPage_{};
};

} // namespace px::panel::ui
