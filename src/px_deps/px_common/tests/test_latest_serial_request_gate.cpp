#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "px_common/latest_serial_request_gate.h"

namespace px {
namespace {

TEST(LatestSerialRequestGate, ReplacementCancelsAndRejectsOldCompletion) {
    const auto gate = LatestSerialRequestGate::Create();
    const auto first = gate->Begin();
    const auto second = gate->Begin();

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_TRUE(first.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(gate->RunIfCurrent(first.generation, []() {}));
    EXPECT_FALSE(gate->Complete(first.generation));
    EXPECT_TRUE(gate->RunIfCurrent(second.generation, []() {}));
    EXPECT_TRUE(gate->Complete(second.generation));
}

TEST(LatestSerialRequestGate, RunningOldRequestFinishesBeforeLatestRequest) {
    const auto gate = LatestSerialRequestGate::Create();
    const auto first = gate->Begin();
    const auto entered = std::make_shared<std::promise<void>>();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();
    const auto order = std::make_shared<std::vector<int>>();
    const auto order_mutex = std::make_shared<std::mutex>();

    std::thread first_thread([gate, first, entered, release_future,
                              order, order_mutex]() {
        static_cast<void>(gate->RunIfCurrent(first.generation, [&]() {
            entered->set_value();
            release_future.wait();
            std::lock_guard lock(*order_mutex);
            order->push_back(1);
        }));
    });
    entered->get_future().wait();

    const auto second = gate->Begin();
    std::thread second_thread([gate, second, order, order_mutex]() {
        static_cast<void>(gate->RunIfCurrent(second.generation, [&]() {
            std::lock_guard lock(*order_mutex);
            order->push_back(2);
        }));
    });
    release->set_value();
    first_thread.join();
    second_thread.join();

    EXPECT_EQ(*order, (std::vector<int>{1, 2}));
    EXPECT_FALSE(gate->Complete(first.generation));
    EXPECT_TRUE(gate->Complete(second.generation));
}

TEST(LatestSerialRequestGate, QueuedSupersededRequestIsSkipped) {
    const auto gate = LatestSerialRequestGate::Create();
    const auto blocker = gate->Begin();
    const auto entered = std::make_shared<std::promise<void>>();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = release->get_future().share();

    std::thread blocker_thread([gate, blocker, entered, release_future]() {
        static_cast<void>(gate->RunIfCurrent(blocker.generation, [&]() {
            entered->set_value();
            release_future.wait();
        }));
    });
    entered->get_future().wait();

    const auto skipped = gate->Begin();
    const auto latest = gate->Begin();
    const auto skipped_calls = std::make_shared<std::atomic_int>(0);
    const auto latest_calls = std::make_shared<std::atomic_int>(0);
    std::thread skipped_thread([gate, skipped, skipped_calls]() {
        static_cast<void>(gate->RunIfCurrent(skipped.generation, [skipped_calls]() {
            ++*skipped_calls;
        }));
    });
    std::thread latest_thread([gate, latest, latest_calls]() {
        static_cast<void>(gate->RunIfCurrent(latest.generation, [latest_calls]() {
            ++*latest_calls;
        }));
    });

    release->set_value();
    blocker_thread.join();
    skipped_thread.join();
    latest_thread.join();

    EXPECT_EQ(skipped_calls->load(), 0);
    EXPECT_EQ(latest_calls->load(), 1);
    EXPECT_TRUE(gate->Complete(latest.generation));
}

TEST(LatestSerialRequestGate, StopCancelsQueuedAndFutureRequests) {
    const auto gate = LatestSerialRequestGate::Create();
    const auto request = gate->Begin();
    gate->Stop();

    ASSERT_TRUE(request.cancellation);
    EXPECT_TRUE(request.cancellation->load(std::memory_order_acquire));
    EXPECT_FALSE(gate->RunIfCurrent(request.generation, []() {}));
    EXPECT_FALSE(gate->Complete(request.generation));
    EXPECT_FALSE(gate->Begin());
}

TEST(LatestSerialRequestGate, ConcurrentBeginsSelectOneLatestRequest) {
    for (int round = 0; round < 10; ++round) {
        const auto gate = LatestSerialRequestGate::Create();
        const auto requests = std::make_shared<
            std::vector<LatestSerialRequestGate::Request>>(32);
        std::vector<std::thread> threads;
        for (std::size_t index = 0; index < requests->size(); ++index) {
            threads.emplace_back([gate, requests, index]() {
                requests->at(index) = gate->Begin();
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        int current = 0;
        for (const auto& request : *requests) {
            if (gate->IsCurrent(request.generation)) {
                ++current;
            }
        }
        EXPECT_EQ(current, 1);
    }
}

} // namespace
} // namespace px
