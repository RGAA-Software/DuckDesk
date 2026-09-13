#include "panel_config_store.h"

#include "px_common/base64.h"
#include "px_common/folder_util.h"
#include "px_common/shared_preference.h"
#include "version_config.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <functional>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace px::panel::product {
namespace {

constexpr std::string_view kAccessPrefix{"console://access##"};
constexpr std::array<unsigned char, 32> kDeploymentKey{'c', 'a', 'e', '8', 'a', 'e', '8', 'C', 'D', 'T', 'D', 'F', '2', '8', '9', '4',
                                                       '3', '7', 'e', '#', '$', '(', ')', '9', '2', 'c', 'b', '1', '7', '5', '4', '0'};

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

std::optional<std::string> DecryptAuthorizationPayload(const std::string& authorization) {
    if (!authorization.starts_with(kAccessPrefix))
        return std::nullopt;
    const std::string encoded{authorization.substr(kAccessPrefix.size())};
    const std::string packed{Base64::Base64Decode(encoded)};
    constexpr std::size_t nonceSize{12};
    constexpr std::size_t tagSize{16};
    if (packed.size() <= nonceSize + tagSize)
        return std::nullopt;

    const auto context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>{EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free};
    if (!context)
        return std::nullopt;
    std::vector<unsigned char> plain(packed.size() - nonceSize - tagSize + EVP_MAX_BLOCK_LENGTH);
    int produced{};
    int tail{};
    const auto cipherSize = static_cast<int>(packed.size() - nonceSize - tagSize);
    const bool initialized =
        EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr, kDeploymentKey.data(), reinterpret_cast<const unsigned char*>(packed.data())) == 1;
    if (!initialized ||
        EVP_DecryptUpdate(context.get(), plain.data(), &produced, reinterpret_cast<const unsigned char*>(packed.data() + nonceSize), cipherSize) !=
            1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tagSize),
                            const_cast<char*>(packed.data() + nonceSize + cipherSize)) != 1 ||
        EVP_DecryptFinal_ex(context.get(), plain.data() + produced, &tail) != 1) {
        return std::nullopt;
    }
    return std::string{reinterpret_cast<const char*>(plain.data()), static_cast<std::size_t>(produced + tail)};
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

bool ConsoleEndpoint::IsValid() const {
    return !host.empty() && port > 0 && port <= 65535 && relayPort > 0 && relayPort <= 65535 && !appKey.empty();
}

std::shared_ptr<PanelConfigStore> PanelConfigStore::Create(const std::filesystem::path& executableDirectory) {
    const auto preferences = SharedPreference::Instance();
    const auto dataDirectory = std::filesystem::path{FolderUtil::GetProgramDataPath()} / "px_data";
    if (!preferences->Init(dataDirectory, "pixels.dat"))
        return {};
    return std::make_shared<PanelConfigStore>(preferences, executableDirectory);
}

PanelConfigStore::PanelConfigStore(std::shared_ptr<SharedPreference> preferences, std::filesystem::path executableDirectory)
    : preferences_{std::move(preferences)}, executableDirectory_{std::move(executableDirectory)} {}

std::optional<ConsoleEndpoint> PanelConfigStore::ParseAuthorization(const std::string& value) const {
    try {
        const auto payload = DecryptAuthorizationPayload(value);
        if (!payload)
            return std::nullopt;
        const auto root = nlohmann::json::parse(*payload);
        const auto& config = root.at("console_srv_config");
        ConsoleEndpoint endpoint{.host = config.at("srv_w3c_ip").get<std::string>(),
                                 .port = config.at("srv_console_port").get<int>(),
                                 .relayPort = config.at("srv_relay_port").get<int>(),
                                 .appKey = config.at("srv_appkey").get<std::string>()};
        return endpoint.IsValid() ? std::optional{std::move(endpoint)} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ConsoleEndpoint> PanelConfigStore::Console() const {
    return ParseAuthorization(Authorization());
}
std::string PanelConfigStore::Authorization() const {
    return Read(preferences_, "console_access_info");
}
std::string PanelConfigStore::NodePublicAddress() const {
    return Read(preferences_, "node_access_host");
}

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

bool PanelConfigStore::SaveNetwork(const std::string& authorization, const std::string& publicAddress, const ConsoleEndpoint& endpoint) {
    const std::scoped_lock lock{mutex_};
    return preferences_->Put("console_access_info", authorization) && preferences_->Put("console_server_host", endpoint.host) &&
           preferences_->PutInt("console_server_port", endpoint.port) && preferences_->Put("relay_server_host", endpoint.host) &&
           preferences_->PutInt("relay_server_port", endpoint.relayPort) && preferences_->Put("node_access_host", publicAddress) &&
           preferences_->Put("console_ssl_enable", "true");
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
    for (const std::string key : {"device_id", "device_name", "device_name_custom", "device_random_pwd", "device_safety_pwd", "console_server_host",
                                  "console_server_port", "relay_server_host", "relay_server_port", "console_access_info", "node_access_host"}) {
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
