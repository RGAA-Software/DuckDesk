#include "connection/udp_media_fallback_state.h"

#include <gtest/gtest.h>

namespace px {

    TEST(UdpMediaFallbackState, FirstMediaLatchesUdpTransport) {
        UdpMediaFallbackState state;
        state.BeginProbe();

        EXPECT_TRUE(state.UsesUdpMedia());
        EXPECT_TRUE(state.MarkUdpMediaReady());
        EXPECT_EQ(state.Current(), UdpMediaTransport::kUdpActive);
        EXPECT_FALSE(state.MarkUdpMediaReady());
    }

    TEST(UdpMediaFallbackState, TimeoutAndWatchdogCanStartOnlyOneFallback) {
        UdpMediaFallbackState state;
        state.BeginProbe();

        EXPECT_TRUE(state.BeginFallback());
        EXPECT_EQ(state.Current(), UdpMediaTransport::kFallbackConnecting);
        EXPECT_FALSE(state.BeginFallback());
        EXPECT_FALSE(state.MarkUdpMediaReady());

        state.MarkWebSocketFallbackActive();
        EXPECT_EQ(state.Current(), UdpMediaTransport::kWebSocketFallback);
        EXPECT_FALSE(state.UsesUdpMedia());
    }

    TEST(UdpMediaFallbackState, StopRejectsLateCallbacks) {
        UdpMediaFallbackState state;
        state.BeginProbe();
        state.Stop();

        EXPECT_EQ(state.Current(), UdpMediaTransport::kStopped);
        EXPECT_FALSE(state.MarkUdpMediaReady());
        EXPECT_FALSE(state.BeginFallback());
        EXPECT_FALSE(state.UsesUdpMedia());
    }

} // namespace px
