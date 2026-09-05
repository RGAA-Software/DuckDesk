#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "px_common_new/concurrent_hashmap.h"
#include "px_common_new/concurrent_queue.h"
#include "px_common_new/concurrent_type.h"
#include "px_common_new/concurrent_vector.h"

namespace px {

TEST(ConcurrentContainers, MapSnapshotCallbackCanMutateContainer) {
    ConcurrentHashMap<int, int> values{};
    values.Insert(1, 10);
    values.Insert(2, 20);

    std::size_t visited{0};
    values.ApplyAll([&](const int& key, const int&) {
        ++visited;
        static_cast<void>(values.Remove(key));
    });

    EXPECT_EQ(visited, 2);
    EXPECT_TRUE(values.Empty());
}

TEST(ConcurrentContainers, MapVisitMutatesStoredValuesWithoutLostWriteback) {
    ConcurrentHashMap<int, int> values{};
    values.Insert(1, 10);
    values.VisitAll([](const int&, int& value) { value += 5; });
    ASSERT_TRUE(values.TryGet(1).has_value());
    EXPECT_EQ(*values.TryGet(1), 15);
    EXPECT_FALSE(values.QueryRange(2, 1).has_value());
}

TEST(ConcurrentContainers, MapConcurrentInsertAndRead) {
    ConcurrentHashMap<int, int> values{};
    constexpr int kThreadCount{4};
    constexpr int kEntriesPerThread{250};
    std::array<std::jthread, kThreadCount> workers{};
    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        workers[thread_index] = std::jthread([&, thread_index] {
            for (int entry = 0; entry < kEntriesPerThread; ++entry) {
                const auto key = thread_index * kEntriesPerThread + entry;
                values.Insert(key, key);
                EXPECT_TRUE(values.TryGet(key).has_value());
            }
        });
    }
    for (auto& worker : workers) worker.join();
    EXPECT_EQ(values.Size(), kThreadCount * kEntriesPerThread);
}

TEST(ConcurrentContainers, VectorCopyReplacesAndShrinksStorage) {
    ConcurrentVector<int> values{};
    values.CopyFrom(std::vector<int>{1, 2, 3, 4});
    EXPECT_TRUE(values.CopyMemFrom(std::vector<int>{7, 8}));
    EXPECT_EQ(values.Clone(), (std::vector<int>{7, 8}));
    EXPECT_FALSE(values.CopyMemPartialFrom(std::vector<int>{1}, 2));
}

TEST(ConcurrentContainers, QueuePopReturnsValueAtomically) {
    ConcurrentQueue<std::string> values{};
    values.PushBack("first");
    values.PushBack("second");
    EXPECT_EQ(values.PopFrontValue(), std::optional<std::string>{"first"});
    EXPECT_EQ(values.PopBackValue(), std::optional<std::string>{"second"});
    EXPECT_FALSE(values.PopFrontValue().has_value());
}

TEST(ConcurrentContainers, ConcurrentTypeStartsValueInitialized) {
    ConcurrentType<std::uint64_t> value{};
    EXPECT_EQ(value.Clone(), 0);
    value.Update(42);
    EXPECT_EQ(value.Clone(), 42);
}

}  // namespace px
