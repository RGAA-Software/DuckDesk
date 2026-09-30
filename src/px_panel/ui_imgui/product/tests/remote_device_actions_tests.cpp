#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>

#include "remote_device_actions.h"

namespace px::panel::ui {
namespace {

class RecordingRemoteControlPort final : public RemoteControlPort {
public:
    RemoteControlState Snapshot() const override { return {}; }
    std::optional<ConnectionProgress> ConnectionProgressSnapshot() const override { return std::nullopt; }
    void Refresh() override {}
    void RefreshTemporaryPassword() override {}
    void SetPasswordVisible(bool) override {}
    void SetIncomingRemoteAccessEnabled(bool) override {}
    void UpdateLocalDeviceName(std::string) override {}
    bool RequiresPassword(const std::string&) const override { return false; }
    bool RequiresDevicePassword(const RemoteDeviceCard&) const override { return false; }
    void Connect(std::string, std::string, bool) override { ++connectionsStarted; }
    void StartStream(const std::string&, std::string, bool) override { ++connectionsStarted; }
    void StopStream(const std::string&) override {}
    void StartFileTransfer(const std::string&, std::string) override { ++connectionsStarted; }
    void SendDeviceCommand(const std::string&, RemoteDeviceCommand) override {}
    void DeleteDevice(const std::string&) override {}
    void SaveDevice(RemoteDeviceCard device) override {
        savedDevice = std::move(device);
        ++settingsSaved;
    }
    void CopyText(const std::string&) override {}
    void OpenUrl(const std::string&) override {}

    std::optional<RemoteDeviceCard> savedDevice{};
    int connectionsStarted{};
    int settingsSaved{};
};

TEST(RemoteDeviceActionsTest, EnablingTcpSelectsDirectWebSocketAndPreservesDeviceSettings) {
    const auto port = std::make_shared<RecordingRemoteControlPort>();
    RemoteDeviceActions actions{port, "recent-devices"};
    const RemoteDeviceCard device{.streamId = "direct-device-uuid",
                                  .name = "Office node",
                                  .deviceId = "device-uuid",
                                  .publicDeviceCode = "934886467",
                                  .consoleOrigin = "https://console.example.test",
                                  .online = true,
                                  .host = "192.168.31.90",
                                  .port = 4601,
                                  .lastConnectedAt = 42,
                                  .audio = true,
                                  .clipboard = true,
                                  .viewOnly = true,
                                  .forceSoftware = true,
                                  .forceRelay = true};
    actions.SetTcpChannelEnabled(device, true);
    ASSERT_TRUE(port->savedDevice);
    const auto& savedDevice = *port->savedDevice;
    EXPECT_TRUE(savedDevice.forceTcp);
    EXPECT_FALSE(savedDevice.forceRelay);
    EXPECT_EQ(savedDevice.streamId, device.streamId);
    EXPECT_EQ(savedDevice.deviceId, device.deviceId);
    EXPECT_EQ(savedDevice.publicDeviceCode, device.publicDeviceCode);
    EXPECT_EQ(savedDevice.consoleOrigin, device.consoleOrigin);
    EXPECT_EQ(savedDevice.name, device.name);
    EXPECT_EQ(savedDevice.host, device.host);
    EXPECT_EQ(savedDevice.port, device.port);
    EXPECT_EQ(savedDevice.lastConnectedAt, device.lastConnectedAt);
    EXPECT_EQ(savedDevice.online, device.online);
    EXPECT_EQ(savedDevice.audio, device.audio);
    EXPECT_EQ(savedDevice.clipboard, device.clipboard);
    EXPECT_EQ(savedDevice.viewOnly, device.viewOnly);
    EXPECT_EQ(savedDevice.forceSoftware, device.forceSoftware);
    EXPECT_EQ(port->settingsSaved, 1);
    EXPECT_EQ(port->connectionsStarted, 0);
    EXPECT_FALSE(device.forceTcp);
    EXPECT_TRUE(device.forceRelay);
}

TEST(RemoteDeviceActionsTest, DisablingTcpReturnsToDefaultDirectMediaWithoutEnablingRelay) {
    const auto port = std::make_shared<RecordingRemoteControlPort>();
    RemoteDeviceActions actions{port, "device-list"};
    actions.SetTcpChannelEnabled({.streamId = "console-device-device-uuid", .deviceId = "device-uuid", .forceTcp = true}, false);
    ASSERT_TRUE(port->savedDevice);
    EXPECT_FALSE(port->savedDevice->forceTcp);
    EXPECT_FALSE(port->savedDevice->forceRelay);
    EXPECT_EQ(port->connectionsStarted, 0);
}

TEST(RemoteDeviceActionsTest, RepeatedTogglesRemainIndependentOfDeviceOnlineStatus) {
    const auto port = std::make_shared<RecordingRemoteControlPort>();
    RemoteDeviceActions actions{port, "recent-devices"};
    RemoteDeviceCard device{.streamId = "direct-device-uuid", .deviceId = "device-uuid", .online = false};
    for (int toggleIndex{}; toggleIndex < 6; ++toggleIndex) {
        const bool enabled{toggleIndex % 2 == 0};
        actions.SetTcpChannelEnabled(device, enabled);
        ASSERT_TRUE(port->savedDevice);
        EXPECT_EQ(port->savedDevice->forceTcp, enabled);
        EXPECT_FALSE(port->savedDevice->forceRelay);
        EXPECT_FALSE(port->savedDevice->online);
        device = *port->savedDevice;
    }
    EXPECT_EQ(port->settingsSaved, 6);
    EXPECT_EQ(port->connectionsStarted, 0);
}

}  // namespace
}  // namespace px::panel::ui
