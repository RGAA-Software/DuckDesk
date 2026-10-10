#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "panel_config_store.h"
#include "px_common/secret_buffer.h"
#include "px_ui/device_platform.h"

namespace px::panel::product {

enum class NativeConnectionKind : std::uint8_t {
    SharedLinkDirect,
    IpDirect,
    Rdp,
};

struct NativeLaunchRequest final {
    NativeConnectionKind connectionKind{NativeConnectionKind::IpDirect};
    std::string displayName{};
    std::string remoteDeviceId{};
    px::ui::DevicePlatform remotePlatform{px::ui::DevicePlatform::Unknown};
    std::string instanceId{};
    std::string nonce{};
    std::string directHost{};
    int directPort{};
    std::string directStreamId{};
    std::string remotePasswordHash{};
    std::string frontendSessionId{};
    std::int64_t frontendSessionRevision{};
    std::shared_ptr<const px::SecretBuffer> frontendToken{};
    std::string relayHost{};
    int relayPort{};
    std::string relayRemoteDeviceId{};
    std::string relayAdmissionTicket{};
    std::shared_ptr<const px::SecretBuffer> rdpConfiguration{};
    bool viewOnly{};
    bool forceTcp{};
    bool forceRelay{};
    bool fileTransfer{};
    bool audio{true};
    bool clipboard{true};
    bool splitWindows{};
    bool forceSoftware{};
    bool waitForDebugger{};
    bool forceGdiCapture{};
    bool disableVulkan{};
};

class PanelClientLauncher final {
public:
    static std::shared_ptr<PanelClientLauncher> Create(const std::shared_ptr<PanelConfigStore>& config);
    explicit PanelClientLauncher(std::shared_ptr<PanelConfigStore> config);
    ~PanelClientLauncher();

    bool Launch(const NativeLaunchRequest& request);
    bool Stop(const std::string& streamId);
    void StopAll();

private:
    struct Process;
    [[nodiscard]] bool LaunchNative(const NativeLaunchRequest& request, const std::string& host, int port);
    [[nodiscard]] bool LaunchRdp(const NativeLaunchRequest& request, const std::string& host, int port);

    std::shared_ptr<PanelConfigStore> config_{};
    std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<Process>> processes_{};
};

}  // namespace px::panel::product
