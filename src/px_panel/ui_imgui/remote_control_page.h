#pragma once

#include "remote_control_port.h"

#include "px_ui/localization.h"

#include <memory>
#include <optional>
#include <string>

namespace px::panel::ui {

class RemoteControlPage final {
  public:
    explicit RemoteControlPage(std::shared_ptr<RemoteControlPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawIdentity(const RemoteControlState& state, const px::ui::Localizer& localizer);
    void DrawConnections(const RemoteControlState& state, const px::ui::Localizer& localizer);
    void DrawDirectPasswordDialog(const px::ui::Localizer& localizer);
    void DrawDeviceEditor(const px::ui::Localizer& localizer);

    std::shared_ptr<RemoteControlPort> port_{};
    std::string remoteDeviceId_{};
    std::string directTarget_{};
    std::string directPassword_{};
    bool openDirectPasswordDialog_{};
    bool directViewOnly_{};
    std::optional<RemoteDeviceCard> editingDevice_{};
};

} // namespace px::panel::ui
