#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "network_settings_model.h"
#include "settings_port.h"

namespace px {
class SharedPreference;
}

namespace px::panel::product {

struct ConsoleEndpoint final {
    std::string baseUrl{};
    std::string host{};
    int port{443};

    [[nodiscard]] bool IsValid() const;
};

[[nodiscard]] std::optional<ConsoleEndpoint> ParseConsoleHttpsOrigin(std::string value);

struct PanelIdentity final {
    std::string deviceId{};
    std::string deviceName{};
    std::string randomPassword{};
    std::string securityPasswordHash{};
};

[[nodiscard]] bool IsValidTemporaryPassword(std::string_view password);
[[nodiscard]] std::optional<std::string> GenerateTemporaryPassword();

struct NodePorts final {
    int service{4603};
    int desktop{4601};
    int applicationFirst{4613};
    int applicationLast{4998};
    int panel{4999};
    int rtcFirst{5000};
    int rtcLast{5031};
};

struct RemoteDevicePreference final {
    std::string name{};
    bool audio{true};
    bool clipboard{true};
    bool viewOnly{};
    bool splitWindows{};
    bool forceSoftware{};
    bool forceTcp{};
    bool forceRelay{};
    bool waitForDebugger{};
    bool forceGdiCapture{};
    bool disableVulkan{};
};

struct RemoteDeviceHistory final {
    std::string deviceId{};
    std::string publicDeviceCode{};
    std::string consoleOrigin{};
    std::string name{};
    std::string host{};
    int port{};
    std::int64_t lastConnectedAt{};
};

struct CloudApplicationPreference final {
    bool forceTcp{};
    bool forceRelay{};
};

class PanelConfigStore final {
public:
    static std::shared_ptr<PanelConfigStore> Create(const std::filesystem::path& executableDirectory, std::string fixedConsoleAddress = {},
                                                    std::string forbiddenConsoleAddress = {});

    PanelConfigStore(std::shared_ptr<SharedPreference> preferences, std::filesystem::path executableDirectory, std::string fixedConsoleAddress = {},
                     std::string forbiddenConsoleAddress = {});

    [[nodiscard]] std::optional<ConsoleEndpoint> ParseConsoleAddress(const std::string& value) const;
    [[nodiscard]] std::optional<ConsoleEndpoint> Console() const;
    [[nodiscard]] std::string ConsoleAddress() const;
    [[nodiscard]] bool ConsoleAddressEditable() const;
    [[nodiscard]] std::string OfficialConsoleAddress() const;
    [[nodiscard]] PanelIdentity Identity() const;
    [[nodiscard]] std::string CachedPublicDeviceCode(const std::string& deviceId) const;
    [[nodiscard]] std::string CachedNodeAccessHost(const std::string& deviceId) const;
    [[nodiscard]] NodePorts Ports() const;
    [[nodiscard]] ui::SettingsSnapshot Settings() const;
    [[nodiscard]] bool ShowTemporaryPassword() const;
    [[nodiscard]] bool IncomingRemoteAccessEnabled() const;
    [[nodiscard]] bool DeviceNameIsCustom() const;
    [[nodiscard]] bool RemoteDeviceHidden(const std::string& deviceId) const;
    [[nodiscard]] std::optional<RemoteDevicePreference> LoadRemoteDevicePreference(const std::string& deviceId) const;
    [[nodiscard]] std::vector<RemoteDeviceHistory> LoadRemoteDeviceHistory() const;
    [[nodiscard]] CloudApplicationPreference LoadCloudApplicationPreference(const std::string& applicationId) const;

    bool SaveNetwork(const std::string& consoleAddress, const ConsoleEndpoint& endpoint);
    bool SaveOfficialNetwork();
    bool SaveIdentity(const PanelIdentity& identity);
    bool SavePublicDeviceCode(const std::string& deviceId, const std::string& publicDeviceCode, const std::string& nodeAccessHost);
    bool SaveCustomDeviceName(const std::string& deviceName);
    bool SaveGeneral(const ui::GeneralSettings& settings);
    bool SaveController(const ui::ControllerSettings& settings);
    bool SaveDisconnectAutoLock(bool enabled);
    bool SaveSecurityPasswordHash(const std::string& hash);
    bool SaveLanguage(::px::ui::Language language);
    bool SaveTheme(::px::ui::Theme theme);
    bool SaveShowTemporaryPassword(bool visible);
    bool SaveIncomingRemoteAccessEnabled(bool enabled);
    bool SaveRemoteDevicePreference(const std::string& deviceId, const RemoteDevicePreference& preference);
    bool DeleteRemoteDevicePreference(const std::string& deviceId);
    bool SaveRemoteDeviceHistory(const RemoteDeviceHistory& device);
    bool DeleteRemoteDeviceHistory(const std::string& deviceId);
    bool HideRemoteDevice(const std::string& deviceId);
    bool UnhideRemoteDevice(const std::string& deviceId);
    bool SaveCloudApplicationPreference(const std::string& applicationId, const CloudApplicationPreference& preference);
    void Clear();

    [[nodiscard]] std::filesystem::path ExecutableDirectory() const;
    [[nodiscard]] std::filesystem::path DataDirectory() const;

private:
    bool SelectConsoleAddress(const std::string& consoleAddress);

    std::shared_ptr<SharedPreference> preferences_{};
    std::filesystem::path executableDirectory_{};
    std::string fixedConsoleAddress_{};
    std::string forbiddenConsoleAddress_{};
    mutable std::mutex mutex_{};
};

}  // namespace px::panel::product
