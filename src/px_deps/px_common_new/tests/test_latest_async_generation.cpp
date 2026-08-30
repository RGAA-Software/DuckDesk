#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "px_common_new/latest_async_generation.h"

namespace px
{
    TEST(LatestAsyncGeneration, OnlyLatestRequestCanComplete) {
        const auto generation = LatestAsyncGeneration::Create();
        const auto first = generation->Begin();
        const auto second = generation->Begin();
        EXPECT_FALSE(generation->Complete(first));
        EXPECT_TRUE(generation->Complete(second));
        EXPECT_FALSE(generation->Complete(second));
    }

    TEST(LatestAsyncGeneration, StopRejectsQueuedAndFutureWork) {
        const auto generation = LatestAsyncGeneration::Create();
        const auto queued = generation->Begin();
        generation->Stop();
        EXPECT_FALSE(generation->Complete(queued));
        EXPECT_EQ(generation->Begin(), 0);
        EXPECT_FALSE(generation->Complete(0));
    }

    TEST(LatestAsyncGeneration, ConcurrentRequestsSelectExactlyOneLatestValue) {
        for (int round = 0; round < 10; ++round) {
            const auto generation = LatestAsyncGeneration::Create();
            const auto values = std::make_shared<std::vector<std::uint64_t>>(32);
            std::vector<std::thread> threads;
            for (std::size_t index = 0; index < values->size(); ++index) {
                threads.emplace_back([generation, values, index]() {
                    values->at(index) = generation->Begin();
                });
            }
            for (auto& thread : threads) thread.join();

            int accepted = 0;
            for (const auto value : *values) {
                if (generation->Complete(value)) ++accepted;
            }
            EXPECT_EQ(accepted, 1);
        }
    }

    TEST(LatestAsyncGeneration, RepeatedStartStopIsDeterministic) {
        for (int round = 0; round < 10; ++round) {
            const auto generation = LatestAsyncGeneration::Create();
            const auto request = generation->Begin();
            EXPECT_TRUE(generation->Complete(request));
            generation->Stop();
            EXPECT_FALSE(generation->Complete(request));
        }
    }
}
