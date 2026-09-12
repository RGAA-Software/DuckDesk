#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"

#include <array>
#include <memory>

namespace px::panel::ui {

class SecuritySettingsPage final {
  public:
    explicit SecuritySettingsPage(std::shared_ptr<SettingsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    std::shared_ptr<SettingsPort> port_{};
    std::array<char, 256> password_{};
    std::array<char, 256> confirmation_{};
    std::array<char, 1024> logDestination_{};
    bool disconnectAutoLock_{false};
    bool loaded_{false};
    bool passwordRejected_{false};
    bool confirmClear_{false};
};

} // namespace px::panel::ui
