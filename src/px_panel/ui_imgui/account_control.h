#pragma once

#include "account_port.h"
#include "px_ui/localization.h"

#include <memory>
#include <string>

namespace px::panel::ui {

class AccountControl final {
  public:
    explicit AccountControl(std::shared_ptr<AccountPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawDialog(const px::ui::Localizer& localizer);
    void DrawProfileDialog(const px::ui::Localizer& localizer, const AccountSnapshot& account);

    std::shared_ptr<AccountPort> port_{};
    std::string username_{};
    std::string password_{};
    std::string confirmation_{};
    bool dialogRequested_{false};
    bool registerMode_{false};
    bool invalidInput_{false};
    bool profileDialogRequested_{false};
    bool profileInitialized_{false};
    bool invalidProfileName_{false};
    bool invalidProfilePassword_{false};
    std::string profileName_{};
    std::string currentPassword_{};
    std::string newPassword_{};
    std::string newPasswordConfirmation_{};
};

} // namespace px::panel::ui
