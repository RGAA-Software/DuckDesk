#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

#include "render_panel/ui/game_catalog_refresh_state.h"

namespace px {
namespace {

TEST(GameCatalogRefreshState, ReplacementCancelsPreviousRequest) {
    const auto state = std::make_shared<GameCatalogRefreshState>();
    const auto first = state->Begin();
    const auto second = state->Begin();

    ASSERT_TRUE(first.cancellation);
    EXPECT_TRUE(first.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(state->IsCurrent(first.generation));
    EXPECT_FALSE(state->Complete(first.generation));
    EXPECT_TRUE(state->IsCurrent(second.generation));
    EXPECT_TRUE(state->Complete(second.generation));
}

TEST(GameCatalogRefreshState, InterimResultDoesNotConsumeGeneration) {
    const auto state = std::make_shared<GameCatalogRefreshState>();
    const auto request = state->Begin();

    EXPECT_TRUE(state->IsCurrent(request.generation));
    EXPECT_TRUE(state->IsCurrent(request.generation));
    EXPECT_TRUE(state->Complete(request.generation));
    EXPECT_FALSE(state->IsCurrent(request.generation));
    EXPECT_FALSE(state->Complete(request.generation));
}

TEST(GameCatalogRefreshState, StopCancelsAndRejectsQueuedCompletion) {
    const auto state = std::make_shared<GameCatalogRefreshState>();
    const auto request = state->Begin();

    state->Stop();

    ASSERT_TRUE(request.cancellation);
    EXPECT_TRUE(request.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(state->IsCurrent(request.generation));
    EXPECT_FALSE(state->Complete(request.generation));
    EXPECT_EQ(state->Begin().generation, 0);
}

TEST(GameCatalogRefreshState, RepeatedStartCompleteAndStopIsDeterministic) {
    for (int round = 0; round < 10; ++round) {
        const auto state = std::make_shared<GameCatalogRefreshState>();
        const auto request = state->Begin();
        EXPECT_TRUE(state->Complete(request.generation));
        EXPECT_FALSE(state->Complete(request.generation));
        state->Stop();
    }
}

TEST(GameCatalogRefreshState, ConcurrentBeginsSelectOneLatestCompletion) {
    for (int round = 0; round < 10; ++round) {
        const auto state = std::make_shared<GameCatalogRefreshState>();
        const auto requests = std::make_shared<
            std::vector<GameCatalogRefreshState::Request>>(24);
        std::vector<std::thread> threads;
        for (std::size_t index = 0; index < requests->size(); ++index) {
            threads.emplace_back([state, requests, index]() {
                requests->at(index) = state->Begin();
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        int completed = 0;
        int not_cancelled = 0;
        for (const auto& request : *requests) {
            ASSERT_TRUE(request.cancellation);
            if (!request.cancellation->load(std::memory_order_acquire)) {
                ++not_cancelled;
            }
            if (state->Complete(request.generation)) {
                ++completed;
            }
        }
        EXPECT_EQ(not_cancelled, 1);
        EXPECT_EQ(completed, 1);
    }
}

} // namespace
} // namespace px
