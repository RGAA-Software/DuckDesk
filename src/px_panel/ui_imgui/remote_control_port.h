#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace px::panel::ui {

enum class RemoteDeviceCommand : std::uint8_t { Lock, Restart, Shutdown };

struct RemoteDeviceCard final {
    std::string streamId{};
    std::string name{};
    std::string deviceId{};
    bool online{false};
    std::string host{};
    int port{0};
    std::string password{};
    std::int64_t lastConnectedAt{};
    bool audio{false};
    bool clipboard{false};
    bool viewOnly{false};
    bool splitWindows{false};
    bool forceSoftware{false};
    bool forceTcp{false};
    bool forceRelay{false};
    bool waitForDebugger{false};
    bool forceGdiCapture{false};
    bool disableVulkan{false};
};

struct RemoteControlState final {
    std::string deviceId{};
    std::string temporaryPassword{};
    std::string deviceName{};
    std::string desktopLink{};
    std::string webClientAddress{};
    bool showTemporaryPassword{false};
    bool managerOnline{false};
    std::vector<RemoteDeviceCard> devices{};
};

class RemoteControlPort {
  public:
    virtual ~RemoteControlPort() = default;
    virtual RemoteControlState Snapshot() const = 0;
    virtual void Refresh() = 0;
    virtual void SetPasswordVisible(bool visible) = 0;
    virtual void UpdateLocalDeviceName(std::string deviceName) = 0;
    [[nodiscard]] virtual bool RequiresPassword(const std::string& target) const = 0;
    virtual void Connect(std::string target, std::string password, bool viewOnly = false) = 0;
    virtual void StartStream(const std::string& streamId, bool viewOnly) = 0;
    virtual void StopStream(const std::string& streamId) = 0;
    virtual void StartFileTransfer(const std::string& streamId) = 0;
    virtual void SendDeviceCommand(const std::string& streamId, RemoteDeviceCommand command) = 0;
    virtual void DeleteDevice(const std::string& streamId) = 0;
    virtual void SaveDevice(RemoteDeviceCard device) = 0;
    virtual void CopyText(const std::string& text) = 0;
    virtual void OpenUrl(const std::string& url) = 0;
};

std::shared_ptr<RemoteControlPort> CreatePreviewRemoteControlPort();

} // namespace px::panel::ui
