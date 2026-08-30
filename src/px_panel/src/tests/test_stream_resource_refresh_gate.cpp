#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "render_panel/devices/stream_resource_refresh_gate.h"

namespace px {

TEST(StreamResourceRefreshGate, NormalRequestsCoalesceWhileActive) {
    const auto gate = StreamResourceRefreshGate::Create();
    const auto generation = gate->Begin();
    ASSERT_TRUE(generation.has_value());
    EXPECT_FALSE(gate->Begin().has_value());
    EXPECT_TRUE(gate->Complete(*generation));
    EXPECT_TRUE(gate->Begin().has_value());
}

TEST(StreamResourceRefreshGate, IdentityChangeSupersedesLateCompletion) {
    const auto gate = StreamResourceRefreshGate::Create();
    const auto old_generation = gate->Begin();
    const auto new_generation = gate->Begin(true);
    ASSERT_TRUE(old_generation.has_value());
    ASSERT_TRUE(new_generation.has_value());
    EXPECT_GT(*new_generation, *old_generation);
    EXPECT_FALSE(gate->Complete(*old_generation));
    EXPECT_TRUE(gate->Complete(*new_generation));
}

TEST(StreamResourceRefreshGate, StopRejectsQueuedCompletionAndNewWork) {
    const auto gate = StreamResourceRefreshGate::Create();
    const auto generation = gate->Begin();
    ASSERT_TRUE(generation.has_value());
    gate->Stop();
    EXPECT_FALSE(gate->Complete(*generation));
    EXPECT_FALSE(gate->Begin().has_value());
    EXPECT_FALSE(gate->Begin(true).has_value());
}

TEST(StreamResourceRefreshGate, ConcurrentNormalRequestsHaveOneStarter) {
    for (int round = 0; round < 10; ++round) {
        const auto gate = StreamResourceRefreshGate::Create();
        const auto starters = std::make_shared<std::atomic_int>(0);
        std::vector<std::thread> threads;
        for (int request = 0; request < 16; ++request) {
            threads.emplace_back([gate, starters]() {
                if (gate->Begin().has_value()) {
                    ++(*starters);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        EXPECT_EQ(starters->load(), 1);
    }
}

TEST(StreamResourceRefreshGate, SupersededOperationsAreSerializedInGenerationOrder) {
    for (int round = 0; round < 10; ++round) {
        const auto gate = StreamResourceRefreshGate::Create();
        const auto first_generation = gate->Begin();
        ASSERT_TRUE(first_generation.has_value());

        const auto first_started = std::make_shared<std::promise<void>>();
        const auto allow_first_finish = std::make_shared<std::promise<void>>();
        auto first_started_future = first_started->get_future();
        auto allow_first_finish_future = allow_first_finish->get_future().share();
        const auto order = std::make_shared<std::vector<int>>();
        const auto order_mutex = std::make_shared<std::mutex>();

        std::thread first([gate, first_started, allow_first_finish_future,
                           order, order_mutex,
                           generation = *first_generation]() mutable {
            EXPECT_TRUE(gate->RunIfCurrent(generation, [
                first_started, allow_first_finish_future, order, order_mutex]() {
                first_started->set_value();
                allow_first_finish_future.wait();
                std::lock_guard lock(*order_mutex);
                order->push_back(1);
            }));
        });
        first_started_future.wait();

        const auto second_generation = gate->Begin(true);
        ASSERT_TRUE(second_generation.has_value());
        std::thread second([gate, order, order_mutex,
                            generation = *second_generation]() {
            EXPECT_TRUE(gate->RunIfCurrent(generation, [order, order_mutex]() {
                std::lock_guard lock(*order_mutex);
                order->push_back(2);
            }));
        });

        allow_first_finish->set_value();
        first.join();
        second.join();
        EXPECT_EQ(*order, (std::vector<int>{1, 2}));
        EXPECT_FALSE(gate->Complete(*first_generation));
        EXPECT_TRUE(gate->Complete(*second_generation));
    }
}

TEST(StreamResourceRefreshGate, RepeatedStartCompleteAndStopAreDeterministic) {
    for (int round = 0; round < 10; ++round) {
        const auto gate = StreamResourceRefreshGate::Create();
        const auto generation = gate->Begin();
        ASSERT_TRUE(generation.has_value());
        EXPECT_TRUE(gate->Complete(*generation));
        const auto queued_generation = gate->Begin();
        ASSERT_TRUE(queued_generation.has_value());
        gate->Stop();
        EXPECT_FALSE(gate->Complete(*queued_generation));
    }
}

} // namespace px
