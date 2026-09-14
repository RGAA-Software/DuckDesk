#pragma once

#include "about_settings_page.h"
#include "controller_settings_page.h"
#include "general_settings_page.h"
#include "network_settings_presenter.h"
#include "security_settings_page.h"

#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace px::panel::ui {

enum class SettingsSection : std::uint8_t { General, Network, Security, Controller, About };

class SettingsPage final {
  public:
    SettingsPage(std::shared_ptr<NetworkSettingsPort> networkPort, std::shared_ptr<SettingsPort> settingsPort,
                 std::shared_ptr<ServerStatusPort> serverStatusPort, std::shared_ptr<SecurityRecordsPort> securityRecordsPort);
    [[nodiscard]] std::optional<px::ui::Theme> Draw(px::ui::Localizer& localizer, px::ui::Theme& theme);

  private:
    SettingsSection selected_{SettingsSection::General};
    GeneralSettingsPage general_;
    NetworkSettingsPresenter network_;
    SecuritySettingsPage security_;
    ControllerSettingsPage controller_;
    AboutSettingsPage about_;
};

} // namespace px::panel::ui
