//
// Tests for tc_common_new/snowflake_id.h
//

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../snowflake_id.h"

namespace snowflake_detail {
struct SnowflakeIdTestAccess {
    static void Reset() {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        SnowflakeId::s_initialized = false;
        SnowflakeId::s_epoch = 0;
        SnowflakeId::s_machine_id = 0;
        SnowflakeId::s_last_timestamp = 0;
        SnowflakeId::s_sequence = 0;
        SnowflakeId::s_clock_fn = SnowflakeId::DefaultEpochMilliseconds;
    }

    static void ResetWithClock(std::function<std::uint64_t()> fn) {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        SnowflakeId::s_initialized = false;
        SnowflakeId::s_epoch = 0;
        SnowflakeId::s_machine_id = 0;
        SnowflakeId::s_last_timestamp = 0;
        SnowflakeId::s_sequence = 0;
        SnowflakeId::s_clock_fn = std::move(fn);
    }

    static void SetClock(std::function<std::uint64_t()> fn) {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        SnowflakeId::s_clock_fn = std::move(fn);
    }

    static std::uint64_t LastTimestamp() {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        return SnowflakeId::s_last_timestamp;
    }

    static std::uint64_t Sequence() {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        return SnowflakeId::s_sequence;
    }

    static std::uint64_t MachineId() {
        std::lock_guard<std::mutex> lock(SnowflakeId::s_mutex);
        return SnowflakeId::s_machine_id;
    }
};
} // namespace snowflake_detail

using snowflake_detail::SnowflakeIdTestAccess;

class SnowflakeIdTest : public ::testing::Test {
protected:
    void SetUp() override {
        SnowflakeIdTestAccess::Reset();
    }
};

TEST_F(SnowflakeIdTest, BasicGenerate) {
    SnowflakeId::initialize(0, 42);
    auto id = SnowflakeId::generate();

    EXPECT_EQ(id.machineId, 42u);
    EXPECT_LE(id.sequence, SnowflakeId::maxSequence());
    EXPECT_GT(id.implode(), 0u);
}

TEST_F(SnowflakeIdTest, MachineIdMasking) {
    SnowflakeId::initialize(0, 1025); // 1025 wraps to 1 within 10 bits
    auto id = SnowflakeId::generate();
    EXPECT_EQ(id.machineId, 1u);
}

TEST_F(SnowflakeIdTest, BitLayoutAndImplode) {
    SnowflakeId id{12345, 777, 888};
    auto packed = id.implode();

    std::uint64_t expected = (12345ULL << 22) | (777ULL << 12) | 888ULL;
    EXPECT_EQ(packed, expected);

    // Extract fields back from packed value.
    std::uint64_t seq  = packed & 0xFFFULL;
    std::uint64_t mid  = (packed >> 12) & 0x3FFULL;
    std::uint64_t ts   = (packed >> 22) & 0x1FFFFFFFFFFULL;
    EXPECT_EQ(seq, 888u);
    EXPECT_EQ(mid, 777u);
    EXPECT_EQ(ts, 12345u);
}

TEST_F(SnowflakeIdTest, UniquenessSingleThread) {
    SnowflakeId::initialize(0, 1);

    constexpr int kCount = 200'000;
    std::vector<std::uint64_t> ids;
    ids.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        ids.push_back(SnowflakeId::generate().implode());
    }

    std::unordered_set<std::uint64_t> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), ids.size());

    // IDs must be strictly increasing because timestamp/sequence always advance.
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
}

TEST_F(SnowflakeIdTest, UniquenessMultiThread) {
    SnowflakeId::initialize(0, 7);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50'000;
    std::vector<std::uint64_t> all_ids;
    all_ids.reserve(kThreads * kPerThread);

    std::vector<std::vector<std::uint64_t>> per_thread(kThreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            per_thread[t].reserve(kPerThread);
            for (int i = 0; i < kPerThread; ++i) {
                per_thread[t].push_back(SnowflakeId::generate().implode());
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    for (auto& vec : per_thread) {
        all_ids.insert(all_ids.end(), vec.begin(), vec.end());
    }

    std::unordered_set<std::uint64_t> unique(all_ids.begin(), all_ids.end());
    EXPECT_EQ(unique.size(), all_ids.size());

    // Each thread's own stream is monotonic.
    for (const auto& vec : per_thread) {
        EXPECT_TRUE(std::is_sorted(vec.begin(), vec.end()));
    }
}

TEST_F(SnowflakeIdTest, SequenceOverflowWaitsForNextMillisecond) {
    // Mock clock: every 4096 calls advance 1 ms, starting at 1000.
    auto counter = std::make_shared<std::atomic<std::uint64_t>>(0);
    SnowflakeIdTestAccess::ResetWithClock([counter]() -> std::uint64_t {
        return 1000 + ((*counter)++) / 4096;
    });
    SnowflakeId::initialize(0, 1);

    constexpr int kCount = 20'000;
    std::vector<std::uint64_t> ids;
    ids.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        ids.push_back(SnowflakeId::generate().implode());
    }

    std::unordered_set<std::uint64_t> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), ids.size());
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

    // Because we generated 20k IDs, the timestamp should have advanced at
    // least kCount / 4096 milliseconds.
    auto last = SnowflakeIdTestAccess::LastTimestamp();
    EXPECT_GE(last, 1000u + kCount / 4096);
}

TEST_F(SnowflakeIdTest, ClockBackwardsRecovery) {
    // Simulate: first ID at t=1000, then clock jumps back to t=990 and
    // slowly catches up (1 ms per call). generate() must wait until t>=1000.
    auto clock_value = std::make_shared<std::atomic<std::uint64_t>>(1000);
    SnowflakeIdTestAccess::ResetWithClock([clock_value]() -> std::uint64_t {
        return clock_value->load();
    });
    SnowflakeId::initialize(0, 1);

    auto first = SnowflakeId::generate();
    EXPECT_EQ(first.timestamp, 1000u);

    // Jump backwards.
    clock_value->store(990);
    // Each subsequent clock read advances by 1 ms.
    SnowflakeIdTestAccess::SetClock([clock_value]() -> std::uint64_t {
        return clock_value->fetch_add(1);
    });

    auto second = SnowflakeId::generate();
    EXPECT_GE(second.timestamp, 1000u);
    EXPECT_GT(second.implode(), first.implode());
}

TEST_F(SnowflakeIdTest, AutoInitializeWhenNotExplicitlyInitialized) {
    // Should not crash and should return valid IDs.
    auto id = SnowflakeId::generate();
    EXPECT_GT(id.implode(), 0u);
    EXPECT_EQ(id.machineId, 0u);
}

TEST_F(SnowflakeIdTest, ReinitializeResetsSequence) {
    SnowflakeId::initialize(0, 3);
    auto id1 = SnowflakeId::generate();

    SnowflakeId::initialize(0, 3);
    auto id2 = SnowflakeId::generate();

    EXPECT_EQ(id1.machineId, 3u);
    EXPECT_EQ(id2.machineId, 3u);
    EXPECT_EQ(id2.sequence, 0u);
    EXPECT_GE(id2.timestamp, id1.timestamp);
}

TEST_F(SnowflakeIdTest, ManyIdsPerMillisecondUseSequence) {
    // Fixed clock at t=5000. Generate fewer IDs than the 12-bit sequence
    // limit so every ID has the same timestamp but a unique sequence.
    SnowflakeIdTestAccess::ResetWithClock([]() -> std::uint64_t { return 5000; });
    SnowflakeId::initialize(0, 5);

    constexpr int kCount = 3000;
    std::vector<std::uint64_t> ids;
    ids.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        ids.push_back(SnowflakeId::generate().implode());
    }

    std::unordered_set<std::uint64_t> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), ids.size());
    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));

    // All IDs share the same timestamp because the clock never advanced.
    auto last_ts = SnowflakeIdTestAccess::LastTimestamp();
    EXPECT_EQ(last_ts, 5000u);
}

TEST_F(SnowflakeIdTest, DifferentMachineIdsDoNotCollideEasily) {
    // Two independent sequences with different machine IDs must not collide
    // within the same millisecond/sequence range.
    SnowflakeId::initialize(0, 10);
    auto a = SnowflakeId::generate().implode();

    SnowflakeId::initialize(0, 11);
    auto b = SnowflakeId::generate().implode();

    EXPECT_NE(a, b);
}
