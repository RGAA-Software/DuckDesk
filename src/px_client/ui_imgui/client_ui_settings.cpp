#include "client_ui_settings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <thread>
#include <utility>

#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/shared_preference.h"
#include "px_common/win32/unique_win_handle.h"

namespace px::client::imgui {

namespace {

const std::string kControllerPositionKey{"float_button_position_ratio"};

std::string PreferenceKey(const ClientUiPreference preference) {
    switch (preference) {
        case ClientUiPreference::StatisticsVisible:
            return "show_statistics";
        case ClientUiPreference::AudioEnabled:
            return "key_audio_status";
        case ClientUiPreference::English:
            return "ui_english";
        case ClientUiPreference::DarkTheme:
            return "ui_dark_theme";
        case ClientUiPreference::Fullscreen:
            return "ui_fullscreen";
    }
    return {};
}

bool ParseRatio(const std::string_view encodedRatio, float& ratio) {
    const auto parsed = std::from_chars(encodedRatio.data(), encodedRatio.data() + encodedRatio.size(), ratio);
    return parsed.ec == std::errc{} && parsed.ptr == encodedRatio.data() + encodedRatio.size();
}

}  // namespace

struct ClientUiSettings::DatabaseAccess final {
    px::UniqueWinHandle lease{};
    // Reverse destruction closes LevelDB before releasing the filesystem lease.
    std::shared_ptr<px::SharedPreference> preferences{};
};

std::unique_ptr<ClientUiSettings::DatabaseAccess> ClientUiSettings::Acquire() const {
    auto access = std::make_unique<DatabaseAccess>();
    if (preferences_) {
        access->preferences = preferences_;
        return access;
    }
    if (directory_.empty() || databaseName_.empty()) return {};
    std::error_code filesystemError{};
    const auto absolutePath = std::filesystem::absolute(directory_ / databaseName_, filesystemError).lexically_normal();
    if (filesystemError) return {};
    auto leasePath = absolutePath;
    leasePath += L".ui.lock";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    // The existing database is under Public. A file lease also coordinates different
    // Windows sessions and path casing, without introducing a second settings store.
    while (true) {
        access->lease = px::UniqueWinHandle{
            CreateFileW(leasePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (access->lease.get() != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        access->lease.reset();
        if (error != ERROR_SHARING_VIOLATION || std::chrono::steady_clock::now() >= deadline) {
            LOGW("event=client.settings outcome=lease_unavailable error={}", error);
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    access->preferences = std::make_shared<px::SharedPreference>();
    if (!access->preferences->Init(directory_, databaseName_)) return {};
    return access;
}

ClientUiSettings::ClientUiSettings(std::shared_ptr<px::SharedPreference> preferences) : preferences_{std::move(preferences)} {}

ClientUiSettings ClientUiSettings::Open(const std::filesystem::path& directory, const std::string_view databaseName) {
    if (directory.empty() || databaseName.empty()) return ClientUiSettings{{}};
    std::error_code filesystemError{};
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) return ClientUiSettings{{}};
    ClientUiSettings settings{{}};
    settings.directory_ = directory;
    settings.databaseName_ = databaseName;
    return settings;
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
    const auto access = Acquire();
    if (!access) return std::nullopt;
    const std::string encodedPosition{access->preferences->Get(kControllerPositionKey)};
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
    if (!position.IsValid()) return false;
    const auto access = Acquire();
    return access && access->preferences->Put(kControllerPositionKey, std::format("{},{}", position.horizontalRatio, position.verticalRatio));
}

std::optional<bool> ClientUiSettings::LoadPreference(const ClientUiPreference preference) const {
    const auto access = Acquire();
    if (!access) return std::nullopt;
    const auto encoded = access->preferences->Get(PreferenceKey(preference));
    if (encoded == "1") return true;
    if (encoded == "0") return false;
    return std::nullopt;
}

bool ClientUiSettings::SavePreference(const ClientUiPreference preference, const bool enabled) const {
    const auto access = Acquire();
    const auto key = PreferenceKey(preference);
    return access && !key.empty() && access->preferences->Put(key, enabled ? "1" : "0");
}

std::optional<int> ClientUiSettings::LoadFrameRate() const {
    const auto access = Acquire();
    if (!access) return std::nullopt;
    const auto encoded = access->preferences->Get("display_frame_rate");
    int framesPerSecond{};
    const auto parsed = std::from_chars(encoded.data(), encoded.data() + encoded.size(), framesPerSecond);
    constexpr std::array allowedRates{15, 30, 60, 90, 120};
    if (parsed.ec != std::errc{} || parsed.ptr != encoded.data() + encoded.size() ||
        std::ranges::find(allowedRates, framesPerSecond) == allowedRates.end())
        return std::nullopt;
    return framesPerSecond;
}

bool ClientUiSettings::SaveFrameRate(const int framesPerSecond) const {
    constexpr std::array allowedRates{15, 30, 60, 90, 120};
    if (std::ranges::find(allowedRates, framesPerSecond) == allowedRates.end()) return false;
    const auto access = Acquire();
    return access && access->preferences->Put("display_frame_rate", std::to_string(framesPerSecond));
}

std::optional<ClientDisplayResolution> ClientUiSettings::LoadResolution() const {
    const auto access = Acquire();
    if (!access) return std::nullopt;
    const auto encoded = access->preferences->Get("display_resolution");
    const auto separator = encoded.find(',');
    if (separator == std::string::npos) return std::nullopt;
    ClientDisplayResolution resolution{};
    const auto widthResult = std::from_chars(encoded.data(), encoded.data() + separator, resolution.width);
    const auto heightResult = std::from_chars(encoded.data() + separator + 1, encoded.data() + encoded.size(), resolution.height);
    if (widthResult.ec != std::errc{} || widthResult.ptr != encoded.data() + separator || heightResult.ec != std::errc{} ||
        heightResult.ptr != encoded.data() + encoded.size() || !resolution.IsValid())
        return std::nullopt;
    return resolution;
}

bool ClientUiSettings::SaveResolution(const ClientDisplayResolution resolution) const {
    if (!resolution.IsValid()) return false;
    const auto access = Acquire();
    return access && access->preferences->Put("display_resolution", std::format("{},{}", resolution.width, resolution.height));
}

}  // namespace px::client::imgui
