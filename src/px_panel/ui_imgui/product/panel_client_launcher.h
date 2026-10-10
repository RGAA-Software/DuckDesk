#pragma once

#include "px_common/iroh_connection_description.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "client_launch_result.h"
#include "panel_config_store.h"
#include "panel_local_server.h"
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
    std::optional<px::IrohConnectionDescription> iroh{};
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
    std::string panelLaunchId{};
    int panelPort{};
};

class PanelClientLauncher final {
public:
    static std::shared_ptr<PanelClientLauncher> Create(const std::shared_ptr<PanelConfigStore>& config,
                                                       const std::shared_ptr<PanelLocalServer>& localServer);
    PanelClientLauncher(std::shared_ptr<PanelConfigStore> config, std::shared_ptr<PanelLocalServer> localServer);
    ~PanelClientLauncher();

    ClientLaunchResult Launch(const NativeLaunchRequest& request);
    bool Stop(const std::string& streamId);
    void StopAll();
    void Shutdown();

private:
    struct Process;
    [[nodiscard]] std::shared_ptr<Process> LaunchNative(const NativeLaunchRequest& request, const std::string& host, int port);
    [[nodiscard]] std::shared_ptr<Process> LaunchRdp(const NativeLaunchRequest& request, const std::string& host, int port);

    std::shared_ptr<PanelConfigStore> config_{};
    std::shared_ptr<PanelLocalServer> localServer_{};
    std::atomic_bool stopping_{};
    std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<Process>> processes_{};
};

}  // namespace px::panel::product
