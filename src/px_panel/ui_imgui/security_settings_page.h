#pragma once

#include "settings_port.h"
#include "security_records_page.h"

#include "px_ui/localization.h"

#include <memory>
#include <string>

namespace px::panel::ui {

class SecuritySettingsPage final {
  public:
    SecuritySettingsPage(std::shared_ptr<SettingsPort> port, std::shared_ptr<SecurityRecordsPort> securityRecordsPort);
    void Draw(const px::ui::Localizer& localizer);

  private:
    std::shared_ptr<SettingsPort> port_{};
    SecurityRecordsPage records_;
    std::string password_{};
    std::string confirmation_{};
    std::string logDestination_{};
    bool disconnectAutoLock_{false};
    bool loaded_{false};
    bool passwordRejected_{false};
    bool confirmClear_{false};
    bool openAccessRecords_{false};
};

} // namespace px::panel::ui
