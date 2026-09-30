#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "client_controller_position.h"

namespace px {
class SharedPreference;
}

namespace px::client::imgui {

// Uses the existing per-connection LevelDB preferences, not a separate UI configuration file.
class ClientUiSettings final {
public:
    explicit ClientUiSettings(std::shared_ptr<px::SharedPreference> preferences);
    [[nodiscard]] static ClientUiSettings Open(const std::filesystem::path& directory, std::string_view databaseName);
    [[nodiscard]] static std::string DatabaseName(std::string_view product, std::string_view consoleOrigin, std::string_view remoteDeviceId,
                                                  std::string_view workspaceAccount);
    [[nodiscard]] std::optional<ControllerPosition> LoadControllerPosition() const;
    [[nodiscard]] bool SaveControllerPosition(const ControllerPosition& position) const;

private:
    std::shared_ptr<px::SharedPreference> preferences_{};
};

}  // namespace px::client::imgui
