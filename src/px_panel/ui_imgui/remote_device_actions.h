#pragma once

#include "remote_control_port.h"

#include "px_ui/localization.h"

#include <memory>
#include <optional>
#include <string>

namespace px::panel::ui {

class RemoteDeviceActions final {
  public:
    RemoteDeviceActions(std::shared_ptr<RemoteControlPort> port, std::string idScope);

    void Start(const RemoteDeviceCard& device, bool viewOnly);
    void Edit(const RemoteDeviceCard& device);
    void FileTransfer(const RemoteDeviceCard& device);
    void Command(const RemoteDeviceCard& device, RemoteDeviceCommand command);
    void Remove(const RemoteDeviceCard& device);
    void DrawContextMenu(const RemoteDeviceCard& device, const px::ui::Localizer& localizer);
    void DrawDialogs(const px::ui::Localizer& localizer);

  private:
    [[nodiscard]] std::string PopupId(std::string_view name) const;
    void DrawPasswordDialog(const px::ui::Localizer& localizer);
    void DrawEditor(const px::ui::Localizer& localizer);
    void DrawCommandConfirmation(const px::ui::Localizer& localizer);
    void DrawRemoveConfirmation(const px::ui::Localizer& localizer);

    std::shared_ptr<RemoteControlPort> port_{};
    std::string idScope_{};
    std::string pendingTarget_{};
    std::string pendingStreamId_{};
    std::string pendingPassword_{};
    bool pendingViewOnly_{};
    bool pendingFileTransfer_{};
    bool openPasswordDialog_{};
    bool openEditor_{};
    bool openCommandConfirmation_{};
    bool openRemoveConfirmation_{};
    std::optional<RemoteDeviceCard> editingDevice_{};
    std::optional<RemoteDeviceCard> commandingDevice_{};
    std::optional<RemoteDeviceCommand> pendingCommand_{};
    std::optional<RemoteDeviceCard> removingDevice_{};
};

} // namespace px::panel::ui
