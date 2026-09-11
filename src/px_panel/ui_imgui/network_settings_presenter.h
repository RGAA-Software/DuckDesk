#pragma once

#include "network_settings_page.h"
#include "network_settings_port.h"

#include <memory>

namespace px::panel::ui {

class NetworkSettingsPresenter final {
  public:
    explicit NetworkSettingsPresenter(std::shared_ptr<NetworkSettingsPort> port);

    void Draw(const px::ui::Localizer& localizer);

  private:
    px::ui::TextId StatusText(NetworkOperation operation) const noexcept;
    void Synchronize();
    void DrawRestartConfirmation(const px::ui::Localizer& localizer);

    std::shared_ptr<NetworkSettingsPort> port_{};
    NetworkSettingsPage page_{};
    NetworkOperation lastOperation_{NetworkOperation::Idle};
};

} // namespace px::panel::ui
