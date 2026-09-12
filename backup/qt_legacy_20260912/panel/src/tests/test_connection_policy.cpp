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
