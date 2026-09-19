#pragma once

#include "network_settings_model.h"

#include <memory>
#include <string>

namespace px::panel::ui {

enum class NetworkOperation {
    Idle,
    InvalidAuthorization,
    InvalidPublicAddress,
    Verifying,
    Verified,
    Saving,
    SavedNeedsRestart,
    Failed,
};

struct NetworkSettingsState final {
    NetworkSettingsDraft settings{};
    NetworkOperation operation{NetworkOperation::Idle};
    std::string detail{};
};

class NetworkSettingsPort {
  public:
    virtual ~NetworkSettingsPort() = default;

    virtual NetworkSettingsState Snapshot() const = 0;
    virtual void ParseAuthorization(std::string authorizationInfo) = 0;
    virtual void Verify(std::string authorizationInfo) = 0;
    virtual void Save(std::string authorizationInfo, std::string nodePublicAddress) = 0;
    virtual void RestartRender() = 0;
    virtual void Acknowledge() = 0;
};

std::shared_ptr<NetworkSettingsPort> CreatePreviewNetworkSettingsPort();

} // namespace px::panel::ui
