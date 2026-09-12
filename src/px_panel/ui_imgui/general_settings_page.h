#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"

#include <memory>

namespace px::panel::ui {

class GeneralSettingsPage final {
  public:
    explicit GeneralSettingsPage(std::shared_ptr<SettingsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void Reload();

    std::shared_ptr<SettingsPort> port_{};
    GeneralSettings draft_{};
    GeneralSaveResult lastResult_{GeneralSaveResult::Saved};
    bool loaded_{false};
    bool showRestartPrompt_{false};
};

} // namespace px::panel::ui
