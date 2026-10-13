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

enum class ClientUiPreference { StatisticsVisible, AudioEnabled, English, DarkTheme, Fullscreen };
struct ClientDisplayResolution final {
    int width{};
    int height{};
    [[nodiscard]] bool IsValid() const noexcept { return width >= 200 && height >= 200 && width <= 8192 && height <= 8192; }
};

// Existing LevelDB settings, scoped to a stable desktop/application/workspace identity.
// Production handles open only for a serialized read/write, so concurrent Clients can share the target.
class ClientUiSettings final {
public:
    explicit ClientUiSettings(std::shared_ptr<px::SharedPreference> preferences);
    [[nodiscard]] static ClientUiSettings Open(const std::filesystem::path& directory, std::string_view databaseName);
    [[nodiscard]] static std::string DatabaseName(std::string_view product, std::string_view consoleOrigin, std::string_view remoteDeviceId,
                                                  std::string_view workspaceAccount);
    [[nodiscard]] std::optional<ControllerPosition> LoadControllerPosition() const;
    [[nodiscard]] bool SaveControllerPosition(const ControllerPosition& position) const;
    [[nodiscard]] std::optional<bool> LoadPreference(ClientUiPreference preference) const;
    [[nodiscard]] bool SavePreference(ClientUiPreference preference, bool enabled) const;
    [[nodiscard]] std::optional<int> LoadFrameRate() const;
    [[nodiscard]] bool SaveFrameRate(int framesPerSecond) const;
    [[nodiscard]] std::optional<ClientDisplayResolution> LoadResolution() const;
    [[nodiscard]] bool SaveResolution(ClientDisplayResolution resolution) const;

private:
    struct DatabaseAccess;
    [[nodiscard]] std::unique_ptr<DatabaseAccess> Acquire() const;
    std::shared_ptr<px::SharedPreference> preferences_{};
    std::filesystem::path directory_{};
    std::string databaseName_{};
};

}  // namespace px::client::imgui
