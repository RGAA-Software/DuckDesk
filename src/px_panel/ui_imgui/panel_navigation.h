#pragma once

#include "panel_page.h"
#include "account_control.h"

#include "px_ui/localization.h"

namespace px::panel::ui {

struct NavigationAction final {
    PanelPage selectedPage{PanelPage::Settings};
    bool exitRequested{false};
};

class PanelNavigation final {
  public:
    explicit PanelNavigation(std::shared_ptr<AccountPort> accountPort);
    NavigationAction Draw(const px::ui::Localizer& localizer);
    PanelPage SelectedPage() const noexcept;

  private:
    AccountControl account_;
    PanelPage selectedPage_{PanelPage::RemoteControl};
};

} // namespace px::panel::ui
