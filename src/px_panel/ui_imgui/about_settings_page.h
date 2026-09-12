#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"

#include <memory>

namespace px::panel::ui {

class AboutSettingsPage final {
  public:
    explicit AboutSettingsPage(std::shared_ptr<SettingsPort> port);
    void Draw(const px::ui::Localizer& localizer) const;

  private:
    std::shared_ptr<SettingsPort> port_{};
};

} // namespace px::panel::ui
