#pragma once

#include <memory>
#include <string>

#include "network_settings_model.h"

namespace px::panel::ui {

enum class NetworkOperation {
    Idle,
    InvalidConsoleAddress,
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
    virtual void ParseConsoleAddress(std::string consoleAddress) = 0;
    virtual void Verify(std::string consoleAddress) = 0;
    virtual void Save(std::string consoleAddress) = 0;
    virtual void RestartRender() = 0;
    virtual void Acknowledge() = 0;
};

std::shared_ptr<NetworkSettingsPort> CreatePreviewNetworkSettingsPort();

} // namespace px::panel::ui
