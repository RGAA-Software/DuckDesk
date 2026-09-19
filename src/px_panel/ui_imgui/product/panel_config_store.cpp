#include "panel_config_store.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "px_common/folder_util.h"
#include "px_common/shared_preference.h"
#include "version_config.h"

namespace px::panel::product {
namespace {

std::string Read(const std::shared_ptr<SharedPreference>& preferences, const std::string& key, const std::string& fallback = {}) {
    return preferences->Get(key, fallback);
}

bool ReadBool(const std::shared_ptr<SharedPreference>& preferences, const std::string& key, const bool fallback) {
    const auto value = Read(preferences, key);
    return value.empty() ? fallback : value == "true";
}

int ReadPositiveInt(const std::shared_ptr<SharedPreference>& preferences, const std::string& key, const int fallback) {
    const int value{preferences->GetInt(key, fallback)};
    return value > 0 ? value : fallback;
}

bool ValidDnsHost(const std::string_view host) {
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
    std::size_t labelStart{};
    while (labelStart < host.size()) {
        const auto labelEnd = host.find('.', labelStart);
        const auto label = host.substr(labelStart, labelEnd == std::string_view::npos ? host.size() - labelStart : labelEnd - labelStart);
        if (label.empty() || label.size() > 63 || !std::isalnum(static_cast<unsigned char>(label.front())) ||
            !std::isalnum(static_cast<unsigned char>(label.back())) ||
            !std::ranges::all_of(label, [](const unsigned char character) { return std::isalnum(character) != 0 || character == '-'; })) {
            return false;
        }
        if (labelEnd == std::string_view::npos) break;
        labelStart = labelEnd + 1;
    }
    return true;
}

bool ValidIpv4Literal(const std::string_view host) {
    std::size_t octetStart{};
    std::size_t octetCount{};
    while (octetStart < host.size()) {
        const auto octetEnd = host.find('.', octetStart);
        const auto octet = host.substr(octetStart, octetEnd == std::string_view::npos ? host.size() - octetStart : octetEnd - octetStart);
        int value{};
        const auto result = std::from_chars(octet.data(), octet.data() + octet.size(), value);
        if (octet.empty() || octet.size() > 3 || result.ec != std::errc{} || result.ptr != octet.data() + octet.size() || value > 255) {
            return false;
        }
        ++octetCount;
        if (octetEnd == std::string_view::npos) break;
        octetStart = octetEnd + 1;
    }
    return octetCount == 4;
}

std::optional<std::size_t> CountIpv6Groups(const std::string_view text, const bool mayContainIpv4) {
    if (text.empty()) return std::size_t{};
    std::size_t groupStart{};
    std::size_t groupCount{};
    while (groupStart < text.size()) {
        const auto groupEnd = text.find(':', groupStart);
        const auto group = text.substr(groupStart, groupEnd == std::string_view::npos ? text.size() - groupStart : groupEnd - groupStart);
        if (group.empty()) return std::nullopt;
        if (group.contains('.')) {
            if (!mayContainIpv4 || groupEnd != std::string_view::npos || !ValidIpv4Literal(group)) return std::nullopt;
            groupCount += 2;
        } else {
            if (group.size() > 4 || !std::ranges::all_of(group, [](const unsigned char character) { return std::isxdigit(character) != 0; })) {
        return std::nullopt;
            }
            ++groupCount;
        }
        if (groupEnd == std::string_view::npos) break;
        groupStart = groupEnd + 1;
    }
    return groupCount;
}

bool ValidIpv6Literal(const std::string_view host) {
    if (host.empty() || host.contains('%') || host.contains(":::")) return false;
    const auto compression = host.find("::");
    if (compression == std::string_view::npos) {
        const auto groups = CountIpv6Groups(host, true);
        return groups && *groups == 8;
    }
    if (host.find("::", compression + 2) != std::string_view::npos) return false;
    const auto leftGroups = CountIpv6Groups(host.substr(0, compression), false);
    const auto rightGroups = CountIpv6Groups(host.substr(compression + 2), true);
    return leftGroups && rightGroups && *leftGroups + *rightGroups < 8;
}

std::optional<int> ParsePort(const std::string_view text) {
    int port{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && port > 0 && port <= 65535 ? std::optional{port} : std::nullopt;
}

std::optional<ConsoleEndpoint> ParseHttpsConsoleAddress(std::string value) {
    constexpr std::string_view prefix{"https://"};
    if (!value.starts_with(prefix)) return std::nullopt;
    value.erase(0, prefix.size());
    if (value.ends_with('/')) value.pop_back();
    if (value.empty() || value.find_first_of("/?#@ \\\t\r\n") != std::string::npos) return std::nullopt;

    std::string host{};
    int port{443};
    bool ipv6{};
    if (value.starts_with('[')) {
        const auto closing = value.find(']');
        if (closing == std::string::npos) return std::nullopt;
        host = value.substr(1, closing - 1);
        ipv6 = true;
        if (closing + 1 < value.size()) {
            if (value[closing + 1] != ':') return std::nullopt;
            const auto parsed = ParsePort(std::string_view{value}.substr(closing + 2));
            if (!parsed) return std::nullopt;
            port = *parsed;
        }
    } else {
        if (std::ranges::count(value, ':') > 1) return std::nullopt;
        const auto separator = value.rfind(':');
        if (separator != std::string::npos) {
            host = value.substr(0, separator);
            const auto parsed = ParsePort(std::string_view{value}.substr(separator + 1));
            if (!parsed) return std::nullopt;
            port = *parsed;
        } else {
            host = value;
        }
    }
    const bool decimalAddress =
        !ipv6 && std::ranges::all_of(host, [](const unsigned char character) { return std::isdigit(character) != 0 || character == '.'; });
    if (!(ipv6 ? ValidIpv6Literal(host) : (decimalAddress ? ValidIpv4Literal(host) : ValidDnsHost(host))) || host == "0.0.0.0" || host == "::") {
        return std::nullopt;
    }
    std::ranges::transform(host, host.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::string normalized{prefix};
    normalized += ipv6 ? "[" + host + "]" : host;
    if (port != 443) normalized += ':' + std::to_string(port);
    return ConsoleEndpoint{.baseUrl = std::move(normalized), .host = std::move(host), .port = port};
}

std::map<std::string, int> ReadNodePortOverrides(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input)
        return {};
    std::map<std::string, int> values{};
    std::string section{};
    std::string line{};
    while (std::getline(input, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        const auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        std::string key{line.substr(0, separator)};
        key.erase(std::remove_if(key.begin(), key.end(), [](const unsigned char value) { return std::isspace(value) != 0; }), key.end());
        std::string text{line.substr(separator + 1)};
        text.erase(std::remove_if(text.begin(), text.end(), [](const unsigned char value) { return std::isspace(value) != 0; }), text.end());
        int value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
            values[section + "." + key] = value;
    }
    return values;
}

} // namespace

bool ConsoleEndpoint::IsValid() const { return !baseUrl.empty() && !host.empty() && port > 0 && port <= 65535; }

std::shared_ptr<PanelConfigStore> PanelConfigStore::Create(const std::filesystem::path& executableDirectory) {
    const auto preferences = SharedPreference::Instance();
    const auto dataDirectory = std::filesystem::path{FolderUtil::GetProgramDataPath()} / "px_data";
    if (!preferences->Init(dataDirectory, "pixels.dat"))
        return {};
    return std::make_shared<PanelConfigStore>(preferences, executableDirectory);
}

PanelConfigStore::PanelConfigStore(std::shared_ptr<SharedPreference> preferences, std::filesystem::path executableDirectory)
    : preferences_{std::move(preferences)}, executableDirectory_{std::move(executableDirectory)} {}

std::optional<ConsoleEndpoint> PanelConfigStore::ParseConsoleAddress(const std::string& value) const { return ParseHttpsConsoleAddress(value); }

std::optional<ConsoleEndpoint> PanelConfigStore::Console() const { return ParseConsoleAddress(ConsoleAddress()); }
std::string PanelConfigStore::ConsoleAddress() const { return Read(preferences_, "console_server_url"); }
PanelIdentity PanelConfigStore::Identity() const {
    return {.deviceId = Read(preferences_, "device_id"),
            .deviceName = Read(preferences_, "device_name"),
            .randomPassword = Read(preferences_, "device_random_pwd"),
            .securityPasswordHash = Read(preferences_, "device_safety_pwd")};
}

NodePorts PanelConfigStore::Ports() const {
    NodePorts ports{};
    const auto values = ReadNodePortOverrides(executableDirectory_ / "px_service.toml");
    const auto read = [&values](const std::string& key, const int fallback) {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    };
    ports.service = read("network.listen_port", ports.service);
    ports.desktop = read("network.desktop_port", ports.desktop);
    ports.panel = read("network.panel_port", ports.panel);
    ports.applicationFirst = read("applications.port_start", ports.applicationFirst);
    ports.applicationLast = read("applications.port_end", ports.applicationLast);
    ports.rtcFirst = read("rtc.port_start", ports.rtcFirst);
    ports.rtcLast = read("rtc.port_end", ports.rtcLast);
    return ports;
}

ui::SettingsSnapshot PanelConfigStore::Settings() const {
    ui::SettingsSnapshot result{};
    result.general = {.bitrateMbps = ReadPositiveInt(preferences_, "encoder_bitrate", 10),
                      .frameRate = ReadPositiveInt(preferences_, "encoder_fps", 60),
                      .codec = Read(preferences_, "encoder_format", "h264") == "h265" ? ui::VideoCodec::H265 : ui::VideoCodec::H264,
                      .resizeEnabled = ReadBool(preferences_, "encoder_res_resize", false),
                      .width = ReadPositiveInt(preferences_, "encoder_width", 1280),
                      .height = ReadPositiveInt(preferences_, "encoder_height", 720),
                      .captureAudio = ReadBool(preferences_, "capture_audio", true)};
    const std::string decoder{Read(preferences_, "prefer_decoder", "Auto")};
    result.controller = {
        .maximizeClient = ReadBool(preferences_, "show_max_window", false),
        .displayClientLogo = ReadBool(preferences_, "display_client_logo", true),
        .colorfulTitleBar = ReadBool(preferences_, "colorful_titlebar", true),
        .maximumScreens = ReadPositiveInt(preferences_, "max_num_of_screen", 2),
        .preferredDecoder = decoder == "software" || decoder == "Software"
                                ? ui::PreferredDecoder::Software
                                : (decoder == "hardware" || decoder == "Hardware" ? ui::PreferredDecoder::Hardware : ui::PreferredDecoder::Automatic),
        .recordingPath = Read(preferences_, "screen_recording_path")};
    result.disconnectAutoLock = ReadBool(preferences_, "disconnect_auto_lock_screen", false);
    result.version = PROJECT_VERSION;
    result.language = Read(preferences_, "panel_ui_language", "zh-CN") == "en" ? ::px::ui::Language::English : ::px::ui::Language::SimplifiedChinese;
    result.theme = Read(preferences_, "panel_ui_theme", "dark") == "light" ? ::px::ui::Theme::Light : ::px::ui::Theme::Dark;
    result.enhancedVisualEffects = ReadBool(preferences_, "panel_ui_enhanced_effects", true);
    return result;
}

bool PanelConfigStore::ShowTemporaryPassword() const {
    return ReadBool(preferences_, "display_random_pwd", true);
}

bool PanelConfigStore::IncomingRemoteAccessEnabled() const {
    return ReadBool(preferences_, "incoming_remote_access_enabled", true);
}

bool PanelConfigStore::DeviceNameIsCustom() const {
    return ReadBool(preferences_, "device_name_custom", false);
}

bool PanelConfigStore::RemoteDeviceHidden(const std::string& deviceId) const {
    return !deviceId.empty() && ReadBool(preferences_, "panel_remote_device_hidden:" + deviceId, false);
}

std::optional<RemoteDevicePreference> PanelConfigStore::LoadRemoteDevicePreference(const std::string& deviceId) const {
    try {
        const std::string value{Read(preferences_, "panel_remote_device:" + deviceId)};
        if (value.empty())
            return std::nullopt;
        const auto root = nlohmann::json::parse(value);
        return RemoteDevicePreference{.name = root.value("name", std::string{}),
                                      .audio = root.value("audio", true),
                                      .clipboard = root.value("clipboard", true),
                                      .viewOnly = root.value("view_only", false),
                                      .splitWindows = root.value("split_windows", false),
                                      .forceSoftware = root.value("force_software", false),
                                      .forceTcp = root.value("force_tcp", false),
                                      .forceRelay = root.value("force_relay", false),
                                      .waitForDebugger = root.value("wait_debug", false),
                                      .forceGdiCapture = root.value("force_gdi", false),
                                      .disableVulkan = root.value("disable_vulkan", false)};
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<RemoteDeviceHistory> PanelConfigStore::LoadRemoteDeviceHistory() const {
    std::vector<RemoteDeviceHistory> result{};
    preferences_->Visit([&result](const std::string& key, const std::string& value) {
        if (!key.starts_with("panel_remote_device_history:"))
            return;
        try {
            const auto root = nlohmann::json::parse(value);
            RemoteDeviceHistory item{.deviceId = root.value("device_id", std::string{}),
                                     .name = root.value("name", std::string{}),
                                     .host = root.value("host", std::string{}),
                                     .port = root.value("port", 0),
                                     .lastConnectedAt = root.value("last_connected_at", std::int64_t{})};
            if (!item.deviceId.empty())
                result.push_back(std::move(item));
        } catch (...) {
        }
    });
    std::ranges::sort(result, std::greater{}, &RemoteDeviceHistory::lastConnectedAt);
    return result;
}

CloudApplicationPreference PanelConfigStore::LoadCloudApplicationPreference(const std::string& applicationId) const {
    try {
        const std::string value{Read(preferences_, "panel_cloud_application:" + applicationId)};
        if (value.empty())
            return {};
        const auto root = nlohmann::json::parse(value);
        return {.forceTcp = root.value("force_tcp", false), .forceRelay = root.value("force_relay", false)};
    } catch (...) {
        return {};
    }
}

bool PanelConfigStore::SaveNetwork(const std::string& consoleAddress, const ConsoleEndpoint& endpoint) {
    if (!endpoint.IsValid() || consoleAddress != endpoint.baseUrl) return false;
    const std::scoped_lock lock{mutex_};
    return preferences_->Put("console_server_url", consoleAddress);
}

bool PanelConfigStore::SaveIdentity(const PanelIdentity& identity) {
    const std::scoped_lock lock{mutex_};
    return preferences_->Put("device_id", identity.deviceId) && preferences_->Put("device_name", identity.deviceName) &&
           preferences_->Put("device_random_pwd", identity.randomPassword) && preferences_->Put("device_safety_pwd", identity.securityPasswordHash);
}

bool PanelConfigStore::SaveCustomDeviceName(const std::string& deviceName) {
    if (deviceName.empty())
        return false;
    const std::scoped_lock lock{mutex_};
    return preferences_->Put("device_name", deviceName) && preferences_->Put("device_name_custom", "true");
}

bool PanelConfigStore::SaveGeneral(const ui::GeneralSettings& settings) {
    const std::scoped_lock lock{mutex_};
    return preferences_->PutInt("encoder_bitrate", settings.bitrateMbps) && preferences_->PutInt("encoder_fps", settings.frameRate) &&
           preferences_->Put("encoder_format", settings.codec == ui::VideoCodec::H265 ? "h265" : "h264") &&
           preferences_->Put("encoder_res_resize", settings.resizeEnabled ? "true" : "false") &&
           preferences_->PutInt("encoder_width", settings.width) && preferences_->PutInt("encoder_height", settings.height) &&
           preferences_->Put("capture_audio", settings.captureAudio ? "true" : "false");
}

bool PanelConfigStore::SaveController(const ui::ControllerSettings& settings) {
    const std::scoped_lock lock{mutex_};
    const std::string decoder = settings.preferredDecoder == ui::PreferredDecoder::Software
                                    ? "Software"
                                    : (settings.preferredDecoder == ui::PreferredDecoder::Hardware ? "Hardware" : "Auto");
    return preferences_->Put("show_max_window", settings.maximizeClient ? "true" : "false") &&
           preferences_->Put("display_client_logo", settings.displayClientLogo ? "true" : "false") &&
           preferences_->Put("colorful_titlebar", settings.colorfulTitleBar ? "true" : "false") &&
           preferences_->PutInt("max_num_of_screen", settings.maximumScreens) && preferences_->Put("prefer_decoder", decoder) &&
           preferences_->Put("screen_recording_path", settings.recordingPath);
}

bool PanelConfigStore::SaveDisconnectAutoLock(const bool enabled) {
    return preferences_->Put("disconnect_auto_lock_screen", enabled ? "true" : "false");
}
bool PanelConfigStore::SaveSecurityPasswordHash(const std::string& hash) {
    return preferences_->Put("device_safety_pwd", hash);
}
bool PanelConfigStore::SaveLanguage(const ::px::ui::Language language) {
    return preferences_->Put("panel_ui_language", language == ::px::ui::Language::English ? "en" : "zh-CN");
}
bool PanelConfigStore::SaveTheme(const ::px::ui::Theme theme) {
    return preferences_->Put("panel_ui_theme", theme == ::px::ui::Theme::Light ? "light" : "dark");
}
bool PanelConfigStore::SaveEnhancedVisualEffects(const bool enabled) {
    return preferences_->Put("panel_ui_enhanced_effects", enabled ? "true" : "false");
}
bool PanelConfigStore::SaveShowTemporaryPassword(const bool visible) {
    return preferences_->Put("display_random_pwd", visible ? "true" : "false");
}
bool PanelConfigStore::SaveIncomingRemoteAccessEnabled(const bool enabled) {
    return preferences_->Put("incoming_remote_access_enabled", enabled ? "true" : "false");
}

bool PanelConfigStore::SaveRemoteDevicePreference(const std::string& deviceId, const RemoteDevicePreference& preference) {
    if (deviceId.empty())
        return false;
    const nlohmann::json root{{"name", preference.name},
                              {"audio", preference.audio},
                              {"clipboard", preference.clipboard},
                              {"view_only", preference.viewOnly},
                              {"split_windows", preference.splitWindows},
                              {"force_software", preference.forceSoftware},
                              {"force_tcp", preference.forceTcp},
                              {"force_relay", preference.forceRelay},
                              {"wait_debug", preference.waitForDebugger},
                              {"force_gdi", preference.forceGdiCapture},
                              {"disable_vulkan", preference.disableVulkan}};
    return preferences_->Put("panel_remote_device:" + deviceId, root.dump());
}

bool PanelConfigStore::DeleteRemoteDevicePreference(const std::string& deviceId) {
    return !deviceId.empty() && preferences_->Remove("panel_remote_device:" + deviceId);
}

bool PanelConfigStore::SaveRemoteDeviceHistory(const RemoteDeviceHistory& device) {
    if (device.deviceId.empty())
        return false;
    const nlohmann::json root{{"device_id", device.deviceId},
                              {"name", device.name},
                              {"host", device.host},
                              {"port", device.port},
                              {"last_connected_at", device.lastConnectedAt}};
    return preferences_->Put("panel_remote_device_history:" + device.deviceId, root.dump());
}

bool PanelConfigStore::DeleteRemoteDeviceHistory(const std::string& deviceId) {
    return !deviceId.empty() && preferences_->Remove("panel_remote_device_history:" + deviceId);
}

bool PanelConfigStore::HideRemoteDevice(const std::string& deviceId) {
    return !deviceId.empty() && preferences_->Put("panel_remote_device_hidden:" + deviceId, "true");
}

bool PanelConfigStore::UnhideRemoteDevice(const std::string& deviceId) {
    if (deviceId.empty())
        return false;
    static_cast<void>(preferences_->Remove("panel_remote_device_hidden:" + deviceId));
    return true;
}

bool PanelConfigStore::SaveCloudApplicationPreference(const std::string& applicationId, const CloudApplicationPreference& preference) {
    if (applicationId.empty())
        return false;
    const nlohmann::json root{{"force_tcp", preference.forceTcp}, {"force_relay", preference.forceRelay}};
    return preferences_->Put("panel_cloud_application:" + applicationId, root.dump());
}

void PanelConfigStore::Clear() {
    for (const std::string key : {"device_id", "device_name", "device_name_custom", "device_random_pwd", "device_safety_pwd", "console_server_url",
                                  "incoming_remote_access_enabled"}) {
        static_cast<void>(preferences_->Remove(key));
    }
    std::vector<std::string> preferenceKeys{};
    preferences_->Visit([&preferenceKeys](const std::string& key, const std::string&) {
        if (key.starts_with("panel_remote_device:") || key.starts_with("panel_remote_device_history:") ||
            key.starts_with("panel_remote_device_hidden:") || key.starts_with("panel_cloud_application:"))
            preferenceKeys.push_back(key);
    });
    for (const auto& key : preferenceKeys)
        static_cast<void>(preferences_->Remove(key));
}

std::filesystem::path PanelConfigStore::ExecutableDirectory() const {
    return executableDirectory_;
}
std::filesystem::path PanelConfigStore::DataDirectory() const {
    return std::filesystem::path{FolderUtil::GetProgramDataPath()} / "px_data";
}

} // namespace px::panel::product
