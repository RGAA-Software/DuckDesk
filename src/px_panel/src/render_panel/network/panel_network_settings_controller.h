#pragma once

#include "network_settings_workflow.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace px {

class PxApplication;
class PxAsyncScope;
class PxSettings;

struct PanelPortRange final {
    int first{};
    int last{};
};

struct PanelNetworkSettings final {
    std::string authorizationInfo{};
    std::string nodePublicAddress{};
    std::optional<int> consolePort{};
    std::optional<int> relayPort{};
    int serviceManagementPort{};
    int desktopConnectionPort{};
    PanelPortRange applicationPorts{};
    PanelPortRange rtcPorts{};
    int panelListeningPort{};
};

enum class PanelNetworkOperation {
    Idle,
    InvalidAuthorization,
    InvalidPublicAddress,
    Verifying,
    Verified,
    Saving,
    SavedNeedsRestart,
    Failed,
};

struct PanelNetworkState final {
    PanelNetworkSettings settings{};
    PanelNetworkOperation operation{PanelNetworkOperation::Idle};
    std::string detail{};
};

class PanelNetworkSettingsController final : public std::enable_shared_from_this<PanelNetworkSettingsController> {
  public:
    static std::shared_ptr<PanelNetworkSettingsController> Create(const std::shared_ptr<PxApplication>& application);

    explicit PanelNetworkSettingsController(const std::shared_ptr<PxApplication>& application);
    ~PanelNetworkSettingsController();

    PanelNetworkSettingsController(const PanelNetworkSettingsController&) = delete;
    PanelNetworkSettingsController& operator=(const PanelNetworkSettingsController&) = delete;

    PanelNetworkState Snapshot() const;
    void ParseAuthorization(std::string authorizationInfo);
    void Verify(std::string authorizationInfo);
    void Save(std::string authorizationInfo, std::string nodePublicAddress);
    void RestartRender();
    void Acknowledge();
    void Stop();

  private:
    std::optional<NetworkEndpointRequest> ParseEndpoint(const std::string& authorizationInfo);
    void CompleteVerify(const NetworkEndpointRequest& endpoint, VerifyNetworkResult result);
    void CompleteSave(const NetworkEndpointRequest& endpoint, bool forceUpdateDeviceId, SaveNetworkResult result);
    std::string DefaultDeviceName() const;

    std::shared_ptr<PxApplication> application_{};
    std::reference_wrapper<PxSettings> settings_;
    std::shared_ptr<PxAsyncScope> requestScope_{};
    std::shared_ptr<LatestSerialRequestGate> verifyGate_{};
    std::shared_ptr<LatestSerialRequestGate> saveGate_{};
    mutable std::mutex stateMutex_{};
    PanelNetworkState state_{};
};

} // namespace px
