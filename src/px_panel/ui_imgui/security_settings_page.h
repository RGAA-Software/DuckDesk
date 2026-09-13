#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"

#include <memory>
#include <string>

namespace px::panel::ui {

class SecuritySettingsPage final {
  public:
    explicit SecuritySettingsPage(std::shared_ptr<SettingsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    std::shared_ptr<SettingsPort> port_{};
    std::string password_{};
    std::string confirmation_{};
    std::string logDestination_{};
    bool disconnectAutoLock_{false};
    bool loaded_{false};
    bool passwordRejected_{false};
    bool confirmClear_{false};
};

} // namespace px::panel::ui
