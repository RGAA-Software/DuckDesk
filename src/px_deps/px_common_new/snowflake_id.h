//
// Robust header-only Snowflake ID generator.
//
// Bit layout (64-bit unsigned):
//   [0]           unused sign bit
//   [1..41]       41-bit timestamp (milliseconds since configured epoch)
//   [42..51]      10-bit machine / datacenter ID
//   [52..63]      12-bit per-millisecond sequence number
//
// Guarantees:
//   - Thread-safe.
//   - Monotonically increasing IDs within the same process (and millisecond).
//   - Sequence overflow waits for the next millisecond instead of wrapping.
//   - Recovers from small system clock backwards jumps (<= 5s).
//   - Auto-initializes with epoch=0, machineId=0 if generate() is called first.
//

#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace snowflake_detail {
struct SnowflakeIdTestAccess;
}

class SnowflakeId
{
public:
    std::uint64_t timestamp : 41;
    std::uint64_t machineId : 10;
    std::uint64_t sequence  : 12;

    SnowflakeId() = default;

    constexpr SnowflakeId(std::uint64_t ts, std::uint64_t mid, std::uint64_t seq) noexcept
        : timestamp(ts), machineId(mid), sequence(seq)
    {
    }

    // Configure the epoch (milliseconds) and the 10-bit machine ID.
    // Call once per process/module before generating IDs.
    static void initialize(std::uint64_t epoch, std::uint64_t machineId);

    // Generate the next unique ID.
    static SnowflakeId generate();

    // Pack the fields back into a 64-bit integer.
    [[nodiscard]] constexpr std::uint64_t implode() const noexcept
    {
        return (timestamp << (MachineIdBits + SequenceBits))
             | (machineId << SequenceBits)
             | sequence;
    }

    [[nodiscard]] static constexpr std::uint64_t maxMachineId() noexcept { return kMachineIdMask; }
    [[nodiscard]] static constexpr std::uint64_t maxSequence() noexcept { return kSequenceMask; }

    friend constexpr bool operator==(const SnowflakeId& lhs, const SnowflakeId& rhs) noexcept
    {
        return lhs.timestamp == rhs.timestamp
            && lhs.machineId == rhs.machineId
            && lhs.sequence == rhs.sequence;
    }

    friend constexpr bool operator!=(const SnowflakeId& lhs, const SnowflakeId& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    friend struct snowflake_detail::SnowflakeIdTestAccess;

    static constexpr int TimestampBits = 41;
    static constexpr int MachineIdBits = 10;
    static constexpr int SequenceBits  = 12;

    static constexpr std::uint64_t kSequenceMask  = (1ULL << SequenceBits)  - 1;
    static constexpr std::uint64_t kMachineIdMask = (1ULL << MachineIdBits) - 1;
    static constexpr std::uint64_t kMaxBackwardMs = 5000;

    static std::uint64_t DefaultEpochMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    static std::uint64_t CurrentTimestampMs() noexcept
    {
        return s_clock_fn() - s_epoch;
    }

    static void WaitUntilTimestampGe(std::uint64_t target)
    {
        while (CurrentTimestampMs() < target) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    static void InitializeLocked(std::uint64_t epoch, std::uint64_t machineId)
    {
        s_epoch = epoch;
        s_machine_id = machineId & kMachineIdMask;
        s_last_timestamp = 0;
        s_sequence = 0;
        s_initialized = true;
    }

    static inline std::mutex s_mutex;
    static inline std::uint64_t s_epoch = 0;
    static inline std::uint64_t s_machine_id = 0;
    static inline std::uint64_t s_last_timestamp = 0;
    static inline std::uint64_t s_sequence = 0;
    static inline bool s_initialized = false;
    static inline std::function<std::uint64_t()> s_clock_fn = DefaultEpochMilliseconds;
};

inline void SnowflakeId::initialize(std::uint64_t epoch, std::uint64_t machineId)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    InitializeLocked(epoch, machineId);
}

inline SnowflakeId SnowflakeId::generate()
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_initialized) {
        InitializeLocked(0, 0);
    }

    std::uint64_t timestamp = CurrentTimestampMs();

    // Recover from system clock going backwards.
    if (timestamp < s_last_timestamp) {
        const std::uint64_t diff = s_last_timestamp - timestamp;
        if (diff <= kMaxBackwardMs) {
            WaitUntilTimestampGe(s_last_timestamp);
            timestamp = CurrentTimestampMs();
        }
        // If the clock is still behind (or jumped back too far), keep monotonic.
        if (timestamp < s_last_timestamp) {
            timestamp = s_last_timestamp;
        }
    }

    if (timestamp == s_last_timestamp) {
        s_sequence = (s_sequence + 1) & kSequenceMask;
        if (s_sequence == 0) {
            // Sequence overflow: wait for the next millisecond.
            WaitUntilTimestampGe(s_last_timestamp + 1);
            timestamp = CurrentTimestampMs();
            if (timestamp <= s_last_timestamp) {
                timestamp = s_last_timestamp + 1;
            }
            s_last_timestamp = timestamp;
        }
    } else { // timestamp > s_last_timestamp
        s_last_timestamp = timestamp;
        s_sequence = 0;
    }

    return SnowflakeId{s_last_timestamp, s_machine_id, s_sequence};
}
