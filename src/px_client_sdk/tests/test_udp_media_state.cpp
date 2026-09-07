#include "connection/udp_media_state.h"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace px {

TEST(UdpMediaState, ProbeFailureIsLatchedWithoutAnotherTransport) {
    UdpMediaState state{};
    EXPECT_FALSE(state.AcceptsMedia());
    EXPECT_FALSE(state.MarkUnavailable());
    ASSERT_TRUE(state.BeginProbe());
    EXPECT_FALSE(state.BeginProbe());
    EXPECT_EQ(state.MarkUnavailable(), UdpMediaFailure::kProbeTimeout);
    EXPECT_EQ(state.Current(), UdpMediaPhase::kUnavailable);
    EXPECT_FALSE(state.AcceptsMedia());
    EXPECT_FALSE(state.MarkReady());
    EXPECT_FALSE(state.MarkUnavailable());
    EXPECT_FALSE(state.BeginProbe());
}

TEST(UdpMediaState, EstablishedMediaLossIsNotAProbeFailure) {
    UdpMediaState state{};
    ASSERT_TRUE(state.BeginProbe());
    EXPECT_TRUE(state.AcceptsMedia());
    ASSERT_TRUE(state.MarkReady());
    EXPECT_EQ(state.Current(), UdpMediaPhase::kActive);
    EXPECT_EQ(state.MarkUnavailable(), UdpMediaFailure::kInterrupted);
    EXPECT_FALSE(state.AcceptsMedia());
}

TEST(UdpMediaState, StopRejectsLateCallbacksAtEveryPartialStartPoint) {
    for (int phase{}; phase < 4; ++phase) {
        UdpMediaState state{};
        if (phase >= 1)
            ASSERT_TRUE(state.BeginProbe());
        if (phase >= 2)
            ASSERT_TRUE(state.MarkReady());
        if (phase >= 3)
            ASSERT_TRUE(state.MarkUnavailable());
        state.Stop();
        state.Stop();
        EXPECT_FALSE(state.BeginProbe());
        EXPECT_FALSE(state.MarkReady());
        EXPECT_FALSE(state.MarkUnavailable());
        EXPECT_FALSE(state.AcceptsMedia());
    }
}

TEST(UdpMediaState, ConcurrentFailureReportsOnlyOnceAcrossRepeatedSessions) {
    for (int iteration{}; iteration < 32; ++iteration) {
        const auto state = std::make_shared<UdpMediaState>();
        const auto reports = std::make_shared<std::atomic_int>(0);
        ASSERT_TRUE(state->BeginProbe());
        std::vector<std::jthread> workers{};
        for (int index{}; index < 4; ++index) {
            workers.emplace_back([state, reports]() {
                if (state->MarkUnavailable())
                    reports->fetch_add(1);
            });
        }
        workers.clear();
        EXPECT_EQ(reports->load(), 1);
        state->Stop();
        EXPECT_FALSE(state->MarkReady());
    }
}

} // namespace px
