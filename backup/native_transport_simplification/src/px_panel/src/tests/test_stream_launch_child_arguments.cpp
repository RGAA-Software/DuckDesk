#include <gtest/gtest.h>

#include "render_panel/devices/stream_launch_child_arguments.h"

namespace px {
namespace {

TEST(StreamLaunchChildArguments, IpDirectWithoutTicketStillPassesPreparedNonce) {
    const auto arguments = BuildStreamLaunchCredentialArguments({
        .connection_nonce = "ip-direct-preparation-nonce",
    });

    ASSERT_EQ(arguments.size(), 1u);
    EXPECT_EQ(arguments.front(), "--connection_nonce=ip-direct-preparation-nonce");
}

TEST(StreamLaunchChildArguments, ConsoleTicketPassesAllCredentialArguments) {
    const auto arguments = BuildStreamLaunchCredentialArguments({
        .connection_ticket = "ticket",
        .connection_nonce = "nonce",
        .connection_instance_id = "instance",
    });

    ASSERT_EQ(arguments.size(), 3u);
    EXPECT_EQ(arguments[0], "--connection_ticket=dGlja2V0");
    EXPECT_EQ(arguments[1], "--connection_nonce=nonce");
    EXPECT_EQ(arguments[2], "--connection_instance_id=instance");
}

TEST(StreamLaunchChildArguments, EmptyCredentialsAddNothing) {
    EXPECT_TRUE(BuildStreamLaunchCredentialArguments({}).empty());
}

} // namespace
} // namespace px
