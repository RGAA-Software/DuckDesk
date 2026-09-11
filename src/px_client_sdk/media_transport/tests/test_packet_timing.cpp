#include "media_transport/packet_timing.h"
#include <gtest/gtest.h>

namespace px::media {
TEST(PacketTiming, IdentityIsAnOwnedCopyOfWireHeader) {
    Packet payload(32);
    wire::Put(payload, 2, 65535, 2);
    payload[20] = 123;
    payload[27] = 0x50;
    payload[29] = 0x30;
    const auto identity = InspectVideoPacket({MediaKind::kVideo, 7, payload});
    payload.clear();
    ASSERT_TRUE(identity);
    EXPECT_EQ(*identity, (VideoPacketIdentity{123, 65535, 3, 7, 1}));
}

TEST(PacketTiming, RejectsAudioAndTruncatedHeaders) {
    const Packet payload(31);
    EXPECT_FALSE(InspectVideoPacket({MediaKind::kVideo, 0, payload}));
    EXPECT_FALSE(InspectVideoPacket({MediaKind::kAudio, 0, payload}));
}

TEST(PacketTiming, StreamIsolationWrapAndThreshold) {
    VideoPacketTiming timing{};
    const VideoPacketIdentity first{0xffffffff, 65535, 2, 7, 0};
    const VideoPacketIdentity next{0, 0, 0, 7, 0};
    EXPECT_FALSE(timing.Observe(first, 0));
    EXPECT_FALSE(timing.Observe({1, 0, 0, 8, 0}, 60000));
    const auto gap = timing.Observe(next, 60001);
    ASSERT_TRUE(gap);
    EXPECT_EQ(gap->previous, first);
    EXPECT_EQ(gap->current, next);
    EXPECT_EQ(gap->current_us - gap->previous_us, 60001);
    EXPECT_FALSE(timing.Observe(next, 110001));
}

TEST(PacketTiming, ResetAndClockRegressionCannotCreateFalseGaps) {
    VideoPacketTiming timing{};
    for (int cycle{}; cycle < 100; ++cycle) {
        EXPECT_FALSE(timing.Observe({}, 100000));
        EXPECT_FALSE(timing.Observe({}, 1));
        timing.Reset();
        EXPECT_FALSE(timing.Observe({}, 900000));
        timing.Reset();
    }
}
} // namespace px::media
