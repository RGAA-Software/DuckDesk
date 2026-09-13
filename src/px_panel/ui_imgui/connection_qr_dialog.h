#pragma once

#include "px_ui/localization.h"

#include <cstdint>
#include <string>
#include <vector>

namespace px::panel::ui {

class ConnectionQrDialog final {
  public:
    void Open(std::string value);
    void Draw(const px::ui::Localizer& localizer);

  private:
    std::string value_{};
    std::vector<std::uint8_t> pixels_{};
    int moduleCount_{};
    bool openRequested_{};
};

} // namespace px::panel::ui
