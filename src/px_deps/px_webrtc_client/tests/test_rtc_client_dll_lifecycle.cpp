#include "px_webrtc_client/rtc_client.h"

#include <gtest/gtest.h>

namespace px {
namespace {

TEST(RtcClientDllLifecycleTest, ConcreteFactorySupportsRepeatedCreateStopAndDestroy) {
    for (int iteration = 0; iteration < 10; ++iteration) {
        auto client = CreateRtcClient();
        ASSERT_TRUE(client);
        EXPECT_TRUE(client->Exit());
        EXPECT_TRUE(client->Exit());
        client.reset();
    }
}

TEST(RtcClientDllLifecycleTest, DestructionWithoutInitializationStopsOwnedWorker) {
    auto client = CreateRtcClient();
    ASSERT_TRUE(client);
    client.reset();
    SUCCEED();
}

} // namespace
} // namespace px
