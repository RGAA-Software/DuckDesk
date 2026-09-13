#pragma once

#include "settings_port.h"

#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <memory>
#include <optional>

namespace px::panel::ui {

class GeneralSettingsPage final {
  public:
    explicit GeneralSettingsPage(std::shared_ptr<SettingsPort> port);
    [[nodiscard]] std::optional<px::ui::Theme> Draw(px::ui::Localizer& localizer, px::ui::Theme& theme);

  private:
    void Reload();

    std::shared_ptr<SettingsPort> port_{};
    GeneralSettings draft_{};
    GeneralSaveResult lastResult_{GeneralSaveResult::Saved};
    bool loaded_{false};
    bool showRestartPrompt_{false};
};

} // namespace px::panel::ui
