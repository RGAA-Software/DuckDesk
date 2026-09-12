#pragma once

#include "remote_device_actions.h"
#include "remote_control_port.h"

#include "px_ui/localization.h"

#include <memory>
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
    void DrawDeviceCard(const RemoteDeviceCard& device, const px::ui::Localizer& localizer, std::size_t index, float width);

    std::shared_ptr<RemoteControlPort> port_{};
    RemoteDeviceActions deviceActions_;
    std::string remoteDeviceId_{};
    std::string directTarget_{};
    std::string directPassword_{};
    std::string localDeviceNameDraft_{};
    bool openDirectPasswordDialog_{};
    bool directViewOnly_{};
    bool openLocalDeviceNameDialog_{};
};

} // namespace px::panel::ui
