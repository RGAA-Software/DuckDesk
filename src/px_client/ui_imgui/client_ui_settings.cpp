#include "client_ui_settings.h"

#include <charconv>
#include <format>
#include <utility>

#include "px_common/md5.h"
#include "px_common/shared_preference.h"

namespace px::client::imgui {

namespace {

const std::string kControllerPositionKey{"float_button_position_ratio"};

bool ParseRatio(const std::string_view encodedRatio, float& ratio) {
    const auto parsed = std::from_chars(encodedRatio.data(), encodedRatio.data() + encodedRatio.size(), ratio);
    return parsed.ec == std::errc{} && parsed.ptr == encodedRatio.data() + encodedRatio.size();
}

}  // namespace

ClientUiSettings::ClientUiSettings(std::shared_ptr<px::SharedPreference> preferences) : preferences_{std::move(preferences)} {}

ClientUiSettings ClientUiSettings::Open(const std::filesystem::path& directory, const std::string_view databaseName) {
    if (directory.empty() || databaseName.empty()) return ClientUiSettings{{}};
    std::error_code filesystemError{};
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) return ClientUiSettings{{}};
    auto preferences = std::make_shared<px::SharedPreference>();
    if (!preferences->Init(directory, databaseName)) return ClientUiSettings{{}};
    return ClientUiSettings{std::move(preferences)};
}

std::string ClientUiSettings::DatabaseName(const std::string_view product, const std::string_view consoleOrigin,
                                           const std::string_view remoteDeviceId, const std::string_view workspaceAccount) {
    // Stable target identity: never use changing stream/session IDs, passwords or the current Render IP/port.
    // Length prefixes avoid ambiguous separators. MD5 is only a filesystem-safe settings name, not a security credential.
    const std::string connectionIdentity{std::format("{}:{}{}:{}{}:{}{}:{}", product.size(), product, consoleOrigin.size(), consoleOrigin,
                                                     remoteDeviceId.size(), remoteDeviceId, workspaceAccount.size(), workspaceAccount)};
    return "app." + px::MD5::Hex(connectionIdentity) + ".dat";
}

std::optional<ControllerPosition> ClientUiSettings::LoadControllerPosition() const {
    if (!preferences_) return std::nullopt;
    const std::string encodedPosition{preferences_->Get(kControllerPositionKey)};
    const auto separator = encodedPosition.find(',');
    if (separator == std::string::npos) return std::nullopt;
    const std::string_view positionView{encodedPosition};
    ControllerPosition position{};
    if (!ParseRatio(positionView.substr(0, separator), position.horizontalRatio) ||
        !ParseRatio(positionView.substr(separator + 1), position.verticalRatio) || !position.IsValid()) {
        return std::nullopt;
    }
    return position;
}

bool ClientUiSettings::SaveControllerPosition(const ControllerPosition& position) const {
    // A single LevelDB value commits both ratios together; an interrupted write cannot mix two positions.
    return preferences_ && position.IsValid() &&
           preferences_->Put(kControllerPositionKey, std::format("{},{}", position.horizontalRatio, position.verticalRatio));
}

}  // namespace px::client::imgui
