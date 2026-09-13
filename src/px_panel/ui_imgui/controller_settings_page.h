#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"

#include <memory>
#include <string>

namespace px::panel::ui {

class ControllerSettingsPage final {
  public:
    explicit ControllerSettingsPage(std::shared_ptr<SettingsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void Reload();

    std::shared_ptr<SettingsPort> port_{};
    ControllerSettings draft_{};
    std::string recordingPath_{};
    bool loaded_{false};
};

} // namespace px::panel::ui
