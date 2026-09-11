#include "media_transport/send_policy.h"
#include "media_transport/video_backlog.h"
#include <gtest/gtest.h>
#include <limits>
#include <memory>

namespace px::media::test {
TEST(SendBudget, TimerJitterDoesNotAccumulateAndLongStallsHaveBoundedCredit) {
    using namespace std::chrono_literals;
    const auto begin = std::chrono::steady_clock::time_point{};
    auto due = begin;
    for (unsigned index{}; index < 1000; ++index)
        due = NextBatchDeadline(due, due + 400us, 1600us);
    EXPECT_EQ(due - begin, 1600ms); // Resetting at each actual wake would incorrectly take 2000 ms.
    const auto late = due + 100ms;
    EXPECT_EQ(NextBatchDeadline(due, late, 1600us) - late, 600us);
}
TEST(SendBudget, ReservesParityAudioAndProtocolWithoutFixedRate) {
    const auto budget = SendBudget::FromTotal(8'000'000, 20);
    EXPECT_EQ(budget.total_bps, 8'000'000);
    EXPECT_EQ(budget.video_bps, 5'208'800);
    EXPECT_EQ(budget.video_wire_bps, 7'136'000);
    EXPECT_EQ(budget.audio_reserve_bps, 864'000);
    EXPECT_LT(budget.video_bps * 120 / 100, budget.video_wire_bps);
    EXPECT_GT(SendBudget::FromTotal(40'000'000, 20).video_wire_bps, 16'000'000);
    EXPECT_LT(SendBudget::FromTotal(4'000'000, 20).video_wire_bps, budget.video_wire_bps);
}
TEST(SendBudget, ExtremeInputsAreInitializedBoundedAndMonotonic) {
    for (const auto requested : {0ULL, 1'000'000ULL, 8'000'000ULL, 1'000'000'000ULL, std::numeric_limits<unsigned long long>::max()}) {
        for (const auto fec : {0U, 20U, 80U, 100U, 1000U}) {
            const auto budget = SendBudget::FromTotal(requested, fec);
            EXPECT_GT(budget.video_bps, 0);
            EXPECT_LT(budget.video_bps, budget.total_bps);
            EXPECT_LE(budget.total_bps, 1'000'000'000);
            EXPECT_GT(budget.VideoDuration(1448).count(), 0);
            EXPECT_GE(budget.VideoDuration(1448 * 20), budget.VideoDuration(1448) * 20);
        }
    }
}
TEST(ReferenceRecovery, CoalescesRequestsAndUsesEncodedHistoryNotCaptureDistance) {
    ReferenceRecovery recovery{};
    for (std::uint64_t timestamp = 10; timestamp <= 100; timestamp += 10)
        recovery.Output(timestamp, timestamp == 10);
    recovery.Request(91);
    recovery.Request(71);
    recovery.Request(81);
    const auto invalidated = recovery.Take(5);
    ASSERT_TRUE(invalidated);
    EXPECT_EQ(*invalidated, (std::vector<std::uint64_t>{80, 90, 100}));
    EXPECT_FALSE(recovery.Pending());
    EXPECT_TRUE(recovery.Take(5)->empty());
}
TEST(ReferenceRecovery, FallsBackWhenNoGoodReferenceRemainsOrBackendUnsupported) {
    for (const auto first : {0ULL, 10ULL, 101ULL}) {
        ReferenceRecovery recovery{};
        recovery.Output(10, true);
        recovery.Output(100, false);
        recovery.Request(first);
        EXPECT_FALSE(recovery.Take(16));
    }
    ReferenceRecovery recovery{};
    recovery.Output(10, true);
    recovery.Output(20, false);
    recovery.Request(11);
    EXPECT_FALSE(recovery.Take(1));
}
TEST(ReferenceRecovery, LostRecoveryOutputCanBeRequestedAgainAndResetIsSafe) {
    ReferenceRecovery recovery{};
    recovery.Output(1, true);
    recovery.Output(3, false);
    recovery.Request(2);
    ASSERT_TRUE(recovery.Take(4));
    recovery.Output(5, false); // Output was lost on the network: do not treat request acceptance as an ACK.
    recovery.Request(2);
    EXPECT_EQ(*recovery.Take(4), (std::vector<std::uint64_t>{3, 5}));
    recovery.Output(7, true);
    recovery.Request(2);
    EXPECT_FALSE(recovery.Take(4));
    recovery.Reset();
    recovery.Reset();
    EXPECT_FALSE(recovery.Pending());
    recovery.Output(100, true);
    recovery.Output(102, false);
    recovery.Request(101);
    EXPECT_EQ(*recovery.Take(4), (std::vector<std::uint64_t>{102}));
}
TEST(ReferenceRecovery, TimestampLimitDoesNotRequireIncrementingAnIntegerRange) {
    ReferenceRecovery recovery{};
    constexpr auto last = std::numeric_limits<std::uint64_t>::max();
    recovery.Output(last - 100, true);
    recovery.Output(last, false);
    recovery.Request(last - 99);
    EXPECT_EQ(*recovery.Take(4), (std::vector<std::uint64_t>{last}));
}

using Backlog = VideoBacklog<std::shared_ptr<int>>;
TEST(VideoBacklog, CountAndByteBackpressureRequireANewIdr) {
    Backlog queue{};
    const auto now = Backlog::Clock::now();
    for (std::size_t index{}; index < Backlog::kMaxFrames; ++index)
        ASSERT_TRUE(queue.Push({"a", std::make_shared<int>(1), 100, true, now}));
    EXPECT_FALSE(queue.Push({"a", {}, 100, false, now}));
    for (std::size_t index{}; index < Backlog::kMaxFrames; ++index) {
        const auto delivery = queue.Pop(now);
        ASSERT_TRUE(delivery);
        EXPECT_TRUE(delivery->discard); // An old queued IDR predates the lost encoded frame.
    }
    EXPECT_EQ(queue.Bytes(), 0);
    ASSERT_TRUE(queue.Push({"a", {}, 100, false, now}));
    EXPECT_TRUE(queue.Pop(now)->discard);
    ASSERT_TRUE(queue.Push({"a", {}, 100, true, now}));
    EXPECT_FALSE(queue.Pop(now)->discard);
    EXPECT_FALSE(queue.Push({"a", {}, Backlog::kMaxBytes + 1, false, now}));
    EXPECT_EQ(queue.Bytes(), 0);
}
TEST(VideoBacklog, ExpiryCannotLeakDependentFramesButAnotherStreamIsUnaffected) {
    Backlog queue{};
    const auto now = Backlog::Clock::now();
    ASSERT_TRUE(queue.Push({"a", {}, 100, false, now - std::chrono::seconds(1)}));
    ASSERT_TRUE(queue.Push({"b", {}, 100, true, now}));
    ASSERT_TRUE(queue.Push({"a", {}, 100, false, now}));
    ASSERT_TRUE(queue.Push({"a", {}, 100, true, now}));
    EXPECT_TRUE(queue.Pop(now)->discard);
    EXPECT_FALSE(queue.Pop(now)->discard);
    EXPECT_TRUE(queue.Pop(now)->discard);
    EXPECT_FALSE(queue.Pop(now)->discard);
}
TEST(VideoBacklog, ShutdownReleasesQueuedOwnershipAndRejectsFurtherWork) {
    Backlog queue{};
    auto value = std::make_shared<int>(7);
    const std::weak_ptr<int> weak = value;
    ASSERT_TRUE(queue.Push({"a", value, 100, true, Backlog::Clock::now()}));
    value.reset();
    EXPECT_FALSE(weak.expired());
    queue.Close();
    queue.Close();
    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(queue.Pop(Backlog::Clock::now()));
    EXPECT_FALSE(queue.Push({"a", {}, 100, true, Backlog::Clock::now()}));
    EXPECT_EQ(queue.Bytes(), 0);
}
} // namespace px::media::test
