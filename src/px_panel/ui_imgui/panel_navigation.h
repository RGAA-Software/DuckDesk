#pragma once

#include "panel_page.h"

#include "px_ui/localization.h"

namespace px::panel::ui {

struct NavigationAction final {
    PanelPage selectedPage{PanelPage::Settings};
    bool exitRequested{false};
};

class PanelNavigation final {
  public:
    NavigationAction Draw(const px::ui::Localizer& localizer);
    PanelPage SelectedPage() const noexcept;

  private:
    PanelPage selectedPage_{PanelPage::Settings};
};

} // namespace px::panel::ui
