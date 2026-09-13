#pragma once

#include "connection_qr_dialog.h"
#include "remote_device_actions.h"
#include "remote_control_port.h"

#include "px_ui/localization.h"
#include "px_desktop_shell/platform_icon_atlas.h"

#include <memory>
#include <string>

namespace px::panel::ui {

class RemoteControlPage final {
  public:
    explicit RemoteControlPage(std::shared_ptr<RemoteControlPort> port);
    void Draw(const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons);

  private:
    void DrawIdentity(const RemoteControlState& state, const px::ui::Localizer& localizer);
    void DrawConnections(const RemoteControlState& state, const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons);
    void DrawDirectPasswordDialog(const px::ui::Localizer& localizer);
    void DrawDeviceCard(const RemoteDeviceCard& device, const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons,
                        std::size_t index, float width);

    std::shared_ptr<RemoteControlPort> port_{};
    RemoteDeviceActions deviceActions_;
    ConnectionQrDialog qrDialog_{};
    std::string remoteDeviceId_{};
    std::string directTarget_{};
    std::string directPassword_{};
    std::string localDeviceNameDraft_{};
    bool openDirectPasswordDialog_{};
    bool directViewOnly_{};
    bool openLocalDeviceNameDialog_{};
};

} // namespace px::panel::ui
