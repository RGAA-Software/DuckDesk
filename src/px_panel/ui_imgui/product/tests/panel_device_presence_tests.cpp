#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "panel_device_presence.h"

namespace px::panel::product {
namespace {

std::vector<ui::RemoteDeviceCard> RecentDeviceCards(const std::string& origin = "https://console.example.test") {
    return {{.deviceId = "device-uuid", .publicDeviceCode = "934886467", .consoleOrigin = origin, .host = "historical-host", .port = 4601}};
}

px_console::ConsolePublicDeviceEndpoint OnlineDeviceEndpoint(const std::string& publicCode) {
    return {.device_id = "device-uuid", .public_code = publicCode, .host = "current-host", .port = 4613};
}

TEST(PanelDevicePresence, ReconstructedCardsRetainConfirmedPresenceDuringBriefNetworkFailure) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    ASSERT_TRUE(devices.front().online);
    devices = RecentDeviceCards();
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kNetworkUnavailable); },
        confirmedAt + std::chrono::seconds{10});
    EXPECT_TRUE(devices.front().online);
    EXPECT_EQ(devices.front().host, "current-host");
    EXPECT_EQ(devices.front().port, 4613);
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kNetworkUnavailable); },
        confirmedAt + std::chrono::seconds{15});
    EXPECT_FALSE(devices.front().online);
}

TEST(PanelDevicePresence, ThrottledReadsAreNotOfflineButCannotRenewTheLastConfirmation) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    const auto throttled = [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kRateLimited); };
    for (int elapsedSeconds{10}; elapsedSeconds < 75; elapsedSeconds += 10) {
        devices = RecentDeviceCards();
        presence.Refresh(devices, "https://console.example.test", throttled, confirmedAt + std::chrono::seconds{elapsedSeconds});
        EXPECT_TRUE(devices.front().online);
    }
    presence.Refresh(devices, "https://console.example.test", throttled, confirmedAt + std::chrono::seconds{75});
    EXPECT_FALSE(devices.front().online);
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt + std::chrono::seconds{76});
    EXPECT_TRUE(devices.front().online);
}

TEST(PanelDevicePresence, DefinitiveOfflineAndInvalidResponsesClearPresenceImmediately) {
    for (const auto failure :
         {px_console::ConsoleApiError::kNotFound, px_console::ConsoleApiError::kForbidden, px_console::ConsoleApiError::kParseJsonFailed}) {
        PanelDevicePresence presence{};
        auto devices = RecentDeviceCards();
        const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
        presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
        ASSERT_TRUE(devices.front().online);
        presence.Refresh(
            devices, "https://console.example.test", [failure](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(failure); },
            confirmedAt + std::chrono::seconds{1});
        EXPECT_FALSE(devices.front().online);
        presence.Refresh(
            devices, "https://console.example.test",
            [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kRateLimited); },
            confirmedAt + std::chrono::seconds{2});
        EXPECT_FALSE(devices.front().online);
    }
}

TEST(PanelDevicePresence, EquivalentConsoleAddressesPreserveObservationsButSwitchingClearsThem) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    const auto unavailable = [](const std::string&) -> PanelDevicePresence::LookupResult {
        return TcErr(px_console::ConsoleApiError::kNetworkUnavailable);
    };
    presence.Refresh(devices, "https://CONSOLE.example.test:443/", unavailable, confirmedAt + std::chrono::seconds{1});
    EXPECT_TRUE(devices.front().online);
    devices = RecentDeviceCards("https://another-console.example.test");
    presence.Refresh(devices, "https://another-console.example.test", unavailable, confirmedAt + std::chrono::seconds{2});
    EXPECT_FALSE(devices.front().online);
    devices = RecentDeviceCards();
    presence.Refresh(devices, "https://console.example.test", unavailable, confirmedAt + std::chrono::seconds{3});
    EXPECT_FALSE(devices.front().online);
}

TEST(PanelDevicePresence, ReassignedUuidAndRemovedCardsCannotReuseAnOnlineObservation) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string& publicCode) {
            return px_console::ConsolePublicDeviceEndpoint{
                .device_id = "replacement-uuid", .public_code = publicCode, .host = "new-host", .port = 4613};
        },
        confirmedAt + std::chrono::seconds{1});
    EXPECT_FALSE(devices.front().online);
    const auto unavailable = [](const std::string&) -> PanelDevicePresence::LookupResult {
        return TcErr(px_console::ConsoleApiError::kNetworkUnavailable);
    };
    presence.Refresh(devices, "https://console.example.test", unavailable, confirmedAt + std::chrono::seconds{2});
    EXPECT_FALSE(devices.front().online);
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt + std::chrono::seconds{3});
    ASSERT_TRUE(devices.front().online);
    devices.clear();
    presence.Refresh(devices, "https://console.example.test", unavailable, confirmedAt + std::chrono::seconds{4});
    devices = RecentDeviceCards();
    presence.Refresh(devices, "https://console.example.test", unavailable, confirmedAt + std::chrono::seconds{5});
    EXPECT_FALSE(devices.front().online);
}

TEST(PanelDevicePresence, RateLimitingCannotResurrectAnAlreadyExpiredObservation) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kNetworkUnavailable); },
        confirmedAt + std::chrono::seconds{15});
    EXPECT_FALSE(devices.front().online);
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kRateLimited); },
        confirmedAt + std::chrono::seconds{16});
    EXPECT_FALSE(devices.front().online);
}

TEST(PanelDevicePresence, StaleDuplicateBindingsDoNotPoisonTheCorrectDeviceObservation) {
    PanelDevicePresence presence{};
    auto devices = RecentDeviceCards();
    devices.push_back({.deviceId = "outdated-uuid", .publicDeviceCode = "934886467", .consoleOrigin = "https://console.example.test"});
    const auto confirmedAt = PanelDevicePresence::Clock::time_point{};
    presence.Refresh(devices, "https://console.example.test", OnlineDeviceEndpoint, confirmedAt);
    EXPECT_TRUE(devices.front().online);
    EXPECT_FALSE(devices.back().online);
    presence.Refresh(
        devices, "https://console.example.test",
        [](const std::string&) -> PanelDevicePresence::LookupResult { return TcErr(px_console::ConsoleApiError::kNetworkUnavailable); },
        confirmedAt + std::chrono::seconds{1});
    EXPECT_TRUE(devices.front().online);
    EXPECT_FALSE(devices.back().online);
}

}  // namespace
}  // namespace px::panel::product
