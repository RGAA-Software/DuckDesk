#include <gtest/gtest.h>

#include "render_panel/upgrade/upgrade_request_state.h"

namespace px {
namespace {

TEST(UpgradeRequestState, ReplacingCheckCancelsAndRejectsOldCompletion) {
    const auto state = std::make_shared<UpgradeRequestState>();
    const auto first = state->BeginCheck();
    const auto second = state->BeginCheck();

    ASSERT_TRUE(first.cancellation);
    EXPECT_TRUE(first.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(state->CompleteCheck(first.generation));
    EXPECT_TRUE(state->CompleteCheck(second.generation));
}

TEST(UpgradeRequestState, CheckAndDownloadHaveIndependentLifetimes) {
    const auto state = std::make_shared<UpgradeRequestState>();
    const auto check = state->BeginCheck();
    const auto download = state->BeginDownload();

    EXPECT_TRUE(state->IsCheckCurrent(check.generation));
    EXPECT_TRUE(state->IsDownloadCurrent(download.generation));
    EXPECT_TRUE(state->CompleteCheck(check.generation));
    EXPECT_TRUE(state->IsDownloadCurrent(download.generation));
    EXPECT_TRUE(state->CompleteDownload(download.generation));
}

TEST(UpgradeRequestState, StopCancelsWorkAndRejectsQueuedCallbacks) {
    const auto state = std::make_shared<UpgradeRequestState>();
    const auto check = state->BeginCheck();
    const auto download = state->BeginDownload();

    state->Stop();

    ASSERT_TRUE(check.cancellation);
    ASSERT_TRUE(download.cancellation);
    EXPECT_TRUE(check.cancellation->load(std::memory_order_acquire));
    EXPECT_TRUE(download.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(state->IsCheckCurrent(check.generation));
    EXPECT_FALSE(state->IsDownloadCurrent(download.generation));
    EXPECT_FALSE(state->CompleteCheck(check.generation));
    EXPECT_FALSE(state->CompleteDownload(download.generation));
    EXPECT_EQ(state->BeginCheck().generation, 0);
    EXPECT_EQ(state->BeginDownload().generation, 0);
}

TEST(UpgradeRequestState, RepeatedStartCompleteAndStopIsDeterministic) {
    for (int round = 0; round < 10; ++round) {
        const auto state = std::make_shared<UpgradeRequestState>();
        const auto request = state->BeginDownload();
        EXPECT_TRUE(state->CompleteDownload(request.generation));
        EXPECT_FALSE(state->CompleteDownload(request.generation));
        state->Stop();
    }
}

} // namespace
} // namespace px
