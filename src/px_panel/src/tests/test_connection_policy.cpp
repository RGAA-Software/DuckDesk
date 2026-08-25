#include <gtest/gtest.h>

#include "../render_panel/devices/connection_policy.h"

namespace policy = px::connection_policy;

TEST(ConnectionPolicy, ConsoleDeviceAndApplicationConnectionsRequireTicket) {
    EXPECT_EQ(policy::Classify("console_ticket", "123456789", "10.0.0.90", 20371),
              policy::LaunchPolicy::kConsoleTicket);
    EXPECT_EQ(policy::Classify("console_app_ticket", "", "10.0.0.90", 20371),
              policy::LaunchPolicy::kConsoleTicket);
}

TEST(ConnectionPolicy, OnlyExplicitHostPortConnectionMayBypassTicket) {
    EXPECT_EQ(policy::Classify("direct", "", "10.0.0.90", 20371),
              policy::LaunchPolicy::kExplicitDirect);
    EXPECT_EQ(policy::Classify("", "", "10.0.0.90", 20371),
              policy::LaunchPolicy::kReject);
    EXPECT_EQ(policy::Classify("direct", "123456789", "10.0.0.90", 20371),
              policy::LaunchPolicy::kReject);
    EXPECT_EQ(policy::Classify("direct", "", "", 20371),
              policy::LaunchPolicy::kReject);
}

TEST(ConnectionPolicy, PasswordBearingSharedLinkMayKeepRemoteDeviceIdentity) {
    EXPECT_EQ(policy::Classify("shared_link_direct", "123456789", "10.0.0.90", 20371),
              policy::LaunchPolicy::kExplicitDirect);
    EXPECT_EQ(policy::Classify("shared_link_direct", "", "10.0.0.90", 20371),
              policy::LaunchPolicy::kReject);
    EXPECT_EQ(policy::Classify("shared_link_direct", "123456789", "", 20371),
              policy::LaunchPolicy::kReject);
    EXPECT_FALSE(policy::IsLegacyManagedConnection("shared_link_direct", "123456789"));
}

TEST(ConnectionPolicy, SharedLinkRouteDependsOnPanelLoginState) {
    EXPECT_FALSE(policy::SharedLinkUsesConsoleTicket("shared_link_direct", false));
    EXPECT_TRUE(policy::SharedLinkUsesConsoleTicket("shared_link_direct", true));
    EXPECT_FALSE(policy::SharedLinkUsesConsoleTicket("direct", true));
    EXPECT_FALSE(policy::SharedLinkUsesConsoleTicket("console_ticket", false));
}

TEST(ConnectionPolicy, LegacyManagedConnectionsAreIdentifiedForRemoval) {
    EXPECT_TRUE(policy::IsLegacyManagedConnection("", "123456789"));
    EXPECT_TRUE(policy::IsLegacyManagedConnection("signaling", "123456789"));
    EXPECT_FALSE(policy::IsLegacyManagedConnection("console_ticket", "123456789"));
    EXPECT_FALSE(policy::IsLegacyManagedConnection("direct", ""));
}

TEST(ConnectionPolicy, ExistingHostPortRowsCanBeNormalizedToExplicitDirect) {
    EXPECT_TRUE(policy::IsUnclassifiedDirectConnection("", "", "127.0.0.1", 20371));
    EXPECT_FALSE(policy::IsUnclassifiedDirectConnection("", "123456789", "127.0.0.1", 20371));
    EXPECT_FALSE(policy::IsUnclassifiedDirectConnection("direct", "", "127.0.0.1", 20371));
}

TEST(ConnectionPolicy, FourConnectionModesAreMutuallyExclusive) {
    EXPECT_EQ(policy::ResolveConnectionMode(false, false, false, false),
              policy::ConnectionMode::kAuto);
    EXPECT_EQ(policy::ResolveConnectionMode(true, false, false, false),
              policy::ConnectionMode::kRelay);
    EXPECT_EQ(policy::ResolveConnectionMode(false, true, false, false),
              policy::ConnectionMode::kDirect);
    EXPECT_EQ(policy::ResolveConnectionMode(false, false, true, false),
              policy::ConnectionMode::kRtc);
    EXPECT_EQ(policy::ResolveConnectionMode(false, false, false, true),
              policy::ConnectionMode::kUdpDirect);
    EXPECT_EQ(policy::ResolveConnectionMode(false, false, true, true),
              policy::ConnectionMode::kInvalid);
}

TEST(ConnectionPolicy, AmbiguousLegacyModeReturnsToAutomatic) {
    bool relay = false;
    bool direct = false;
    bool rtc = true;
    bool udp = true;

    EXPECT_TRUE(policy::NormalizeConnectionMode(relay, direct, rtc, udp));
    EXPECT_FALSE(relay);
    EXPECT_FALSE(direct);
    EXPECT_FALSE(rtc);
    EXPECT_FALSE(udp);
    EXPECT_EQ(policy::ResolveConnectionMode(relay, direct, rtc, udp),
              policy::ConnectionMode::kAuto);
}

TEST(ConnectionPolicy, ForcedModeNeverChangesTransportFamily) {
    using Mode = policy::ConnectionMode;
    using Transport = policy::SelectedTransport;

    EXPECT_EQ(policy::SelectTransport(Mode::kRelay, true, true, true),
              Transport::kRelay);
    EXPECT_EQ(policy::SelectTransport(Mode::kRelay, true, true, false),
              Transport::kUnavailable);
    EXPECT_EQ(policy::SelectTransport(Mode::kDirect, true, true, true),
              Transport::kWebSocket);
    EXPECT_EQ(policy::SelectTransport(Mode::kDirect, true, false, true),
              Transport::kUnavailable);
    EXPECT_EQ(policy::SelectTransport(Mode::kRtc, true, true, true),
              Transport::kWebRtcDirect);
    EXPECT_EQ(policy::SelectTransport(Mode::kRtc, true, false, true),
              Transport::kWebRtcStandard);
    EXPECT_EQ(policy::SelectTransport(Mode::kUdpDirect, true, true, true),
              Transport::kUdpDirect);
    EXPECT_EQ(policy::SelectTransport(Mode::kUdpDirect, true, false, true),
              Transport::kUnavailable);
}

TEST(ConnectionPolicy, AutomaticModeUsesAvailabilityAndLoginState) {
    using Mode = policy::ConnectionMode;
    using Transport = policy::SelectedTransport;

    EXPECT_EQ(policy::SelectTransport(Mode::kAuto, true, true, true),
              Transport::kWebRtcDirect);
    EXPECT_EQ(policy::SelectTransport(Mode::kAuto, true, false, true),
              Transport::kWebRtcStandard);
    EXPECT_EQ(policy::SelectTransport(Mode::kAuto, false, true, true),
              Transport::kWebSocket);
    EXPECT_EQ(policy::SelectTransport(Mode::kAuto, false, false, true),
              Transport::kWebRtcStandard);
    EXPECT_EQ(policy::SelectTransport(Mode::kAuto, false, false, false),
              Transport::kUnavailable);
}
