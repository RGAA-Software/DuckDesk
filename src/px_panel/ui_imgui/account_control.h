#pragma once

#include "account_port.h"
#include "px_ui/localization.h"

#include <array>
#include <memory>

namespace px::panel::ui {

class AccountControl final {
  public:
    explicit AccountControl(std::shared_ptr<AccountPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawDialog(const px::ui::Localizer& localizer);

    std::shared_ptr<AccountPort> port_{};
    std::array<char, 128> username_{};
    std::array<char, 128> password_{};
    std::array<char, 128> confirmation_{};
    bool dialogRequested_{false};
    bool registerMode_{false};
    bool invalidInput_{false};
};

} // namespace px::panel::ui
