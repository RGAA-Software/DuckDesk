#include "remote_control_port.h"

#include <SDL3/SDL.h>

#include <utility>

namespace px::panel::ui {
namespace {

class PreviewRemoteControlPort final : public RemoteControlPort {
  public:
    RemoteControlState Snapshot() const override {
        return {
            .deviceId = "109022351",
            .temporaryPassword = "1706",
            .deviceName = deviceName_,
            .desktopLink = "link://preview",
            .webClientAddress = "http://192.168.1.10:4601/web/",
            .showTemporaryPassword = passwordVisible_,
            .incomingRemoteAccessEnabled = incomingRemoteAccessEnabled_,
            .managerOnline = true,
            .devices =
                {{.streamId = "preview-90", .name = "Pixels node90", .deviceId = "90", .platform = px::ui::DevicePlatform::Windows, .online = true}},
        };
    }

    void SetPasswordVisible(const bool visible) override {
        passwordVisible_ = visible;
    }
    void SetIncomingRemoteAccessEnabled(const bool enabled) override {
        incomingRemoteAccessEnabled_ = enabled;
    }
    void UpdateLocalDeviceName(std::string deviceName) override {
        if (!deviceName.empty())
            deviceName_ = std::move(deviceName);
    }
    void Refresh() override {}
    void RefreshTemporaryPassword() override {}
    bool RequiresPassword(const std::string&) const override {
        return false;
    }
    void Connect(std::string, std::string, bool) override {}
    void StartStream(const std::string&, bool) override {}
    void StopStream(const std::string&) override {}
    void StartFileTransfer(const std::string&, std::string) override {}
    void SendDeviceCommand(const std::string&, RemoteDeviceCommand) override {}
    void DeleteDevice(const std::string&) override {}
    void SaveDevice(RemoteDeviceCard) override {}
    void CopyText(const std::string& text) override {
        SDL_SetClipboardText(text.c_str());
    }
    void OpenUrl(const std::string& url) override {
        SDL_OpenURL(url.c_str());
    }

  private:
    bool passwordVisible_{false};
    bool incomingRemoteAccessEnabled_{true};
    std::string deviceName_{"MC-10"};
};

} // namespace

std::shared_ptr<RemoteControlPort> CreatePreviewRemoteControlPort() {
    return std::make_shared<PreviewRemoteControlPort>();
}

} // namespace px::panel::ui
