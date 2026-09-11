#pragma once

#include <array>

#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

namespace px::panel::ui {

class PanelPreview final {
  public:
    void Draw();

  private:
    void DrawNavigation();
    void DrawNetworkPage();

    std::array<char, 2048> authorizationInfo_{};
    std::array<char, 256> publicAddress_{};
    px::ui::Localizer localizer_{};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    px::ui::TextId status_{px::ui::TextId::PreviewInitialStatus};
};

} // namespace px::panel::ui
