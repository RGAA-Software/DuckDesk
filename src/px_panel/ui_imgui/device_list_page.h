#pragma once

#include "remote_device_actions.h"
#include "remote_control_port.h"

#include "px_ui/localization.h"

#include <memory>
#include <string>

namespace px::desktop {
class PlatformIconAtlas;
}

namespace px::panel::ui {

class DeviceListPage final {
  public:
    explicit DeviceListPage(std::shared_ptr<RemoteControlPort> port);
    void Draw(const px::ui::Localizer& localizer, const px::desktop::PlatformIconAtlas& platformIcons);

  private:
    std::shared_ptr<RemoteControlPort> port_{};
    RemoteDeviceActions deviceActions_;
    std::string search_{};
    std::string selectedDeviceId_{};
};

} // namespace px::panel::ui
