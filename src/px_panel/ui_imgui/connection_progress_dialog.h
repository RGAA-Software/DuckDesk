#pragma once

#include "remote_control_port.h"

#include "px_ui/localization.h"

#include <cstdint>
#include <memory>

namespace px::panel::ui {

class ConnectionProgressDialog final {
  public:
    explicit ConnectionProgressDialog(std::shared_ptr<RemoteControlPort> port);

    void Draw(const px::ui::Localizer& localizer);

  private:
    std::shared_ptr<RemoteControlPort> port_{};
    std::uint64_t observedGeneration_{0};
};

} // namespace px::panel::ui
