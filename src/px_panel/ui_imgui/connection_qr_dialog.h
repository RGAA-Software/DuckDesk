#pragma once

#include "px_ui/localization.h"

#include <cstdint>
#include <string>
#include <vector>

namespace px::panel::ui {

enum class ConnectionQrKind { DesktopLink, WebClientAddress };

class ConnectionQrDialog final {
  public:
    void Open(std::string value, ConnectionQrKind kind);
    void Draw(const px::ui::Localizer& localizer);

  private:
    std::string value_{};
    std::vector<std::uint8_t> pixels_{};
    ConnectionQrKind kind_{ConnectionQrKind::DesktopLink};
    int moduleCount_{};
    bool openRequested_{};
};

} // namespace px::panel::ui
