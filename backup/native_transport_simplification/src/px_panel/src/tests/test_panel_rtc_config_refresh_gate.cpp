#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "render_panel/network/panel_rtc_config_refresh_gate.h"

namespace px {

TEST(PanelRtcConfigRefreshGate, FirstRequestStartsOneAttempt) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    EXPECT_EQ(gate->Request(7), PanelRtcConfigRefreshRequest::kStarted);
    const auto attempt = gate->CurrentAttempt();
    EXPECT_EQ(attempt.sequence, 1);
    EXPECT_EQ(attempt.expected_revision, 7);
    EXPECT_FALSE(gate->FinishAttempt(attempt.sequence));
}

TEST(PanelRtcConfigRefreshGate, ConcurrentRevisionIsCoalescedIntoFollowUp) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    ASSERT_EQ(gate->Request(4), PanelRtcConfigRefreshRequest::kStarted);
    const auto first = gate->CurrentAttempt();
    EXPECT_EQ(gate->Request(9), PanelRtcConfigRefreshRequest::kCoalesced);
    EXPECT_TRUE(gate->FinishAttempt(first.sequence));
    const auto follow_up = gate->CurrentAttempt();
    EXPECT_GT(follow_up.sequence, first.sequence);
    EXPECT_EQ(follow_up.expected_revision, 9);
    EXPECT_FALSE(gate->FinishAttempt(follow_up.sequence));
}

TEST(PanelRtcConfigRefreshGate, LowerRevisionCannotReplaceLatestTarget) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    ASSERT_EQ(gate->Request(12), PanelRtcConfigRefreshRequest::kStarted);
    const auto first = gate->CurrentAttempt();
    EXPECT_EQ(gate->Request(5), PanelRtcConfigRefreshRequest::kCoalesced);
    EXPECT_TRUE(gate->FinishAttempt(first.sequence));
    EXPECT_EQ(gate->CurrentAttempt().expected_revision, 12);
}

TEST(PanelRtcConfigRefreshGate, AbortAllowsAReplacementWorkflow) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    ASSERT_EQ(gate->Request(1), PanelRtcConfigRefreshRequest::kStarted);
    gate->AbortStart();
    EXPECT_EQ(gate->Request(2), PanelRtcConfigRefreshRequest::kStarted);
}

TEST(PanelRtcConfigRefreshGate, StopRejectsNewAndLateCompletion) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    ASSERT_EQ(gate->Request(3), PanelRtcConfigRefreshRequest::kStarted);
    const auto attempt = gate->CurrentAttempt();
    gate->Stop();
    EXPECT_FALSE(gate->FinishAttempt(attempt.sequence));
    EXPECT_EQ(gate->Request(4), PanelRtcConfigRefreshRequest::kStopped);
}

TEST(PanelRtcConfigRefreshGate, ConcurrentRequestsHaveOneStarterAndLatestRevision) {
    const auto gate = PanelRtcConfigRefreshGate::Create();
    const auto starters = std::make_shared<std::atomic_int>(0);
    std::vector<std::thread> threads;
    for (std::uint64_t revision = 1; revision <= 32; ++revision) {
        threads.emplace_back([gate, revision, starters]() {
            if (gate->Request(revision) == PanelRtcConfigRefreshRequest::kStarted) {
                ++(*starters);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(starters->load(), 1);
    EXPECT_EQ(gate->CurrentAttempt().expected_revision, 32);
}

TEST(PanelRtcConfigRefreshGate, RepeatedStartStopCyclesAreDeterministic) {
    for (int round = 0; round < 10; ++round) {
        const auto gate = PanelRtcConfigRefreshGate::Create();
        ASSERT_EQ(gate->Request(static_cast<std::uint64_t>(round + 1)),
                  PanelRtcConfigRefreshRequest::kStarted);
        gate->Stop();
        EXPECT_EQ(gate->Request(100), PanelRtcConfigRefreshRequest::kStopped);
    }
}

} // namespace px
