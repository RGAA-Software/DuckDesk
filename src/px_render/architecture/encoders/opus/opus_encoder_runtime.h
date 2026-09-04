#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace px {

class Data;

class OpusEncoderRuntime final {
public:
    struct Config final {
        bool debug_decoder = false;
    };

    using EncodedDelivery = std::function<void(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits,
        int frame_size)>;

    static std::shared_ptr<OpusEncoderRuntime> Make(Config config);
    ~OpusEncoderRuntime();

    OpusEncoderRuntime(const OpusEncoderRuntime&) = delete;
    OpusEncoderRuntime& operator=(const OpusEncoderRuntime&) = delete;

    void SetDelivery(EncodedDelivery delivery);
    void ClearDelivery();
    void Enqueue(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits);
    [[nodiscard]] bool IsAccepting() const;
    [[nodiscard]] uint64_t DroppedCount() const;
    void Shutdown();

private:
    struct ConstructionToken final {};
    struct DeliveryChannel final {
        void Set(EncodedDelivery delivery);
        void Clear();
        void Disable();
        void Deliver(
            const std::shared_ptr<Data>& data,
            int sample_rate,
            int channels,
            int bits,
            int frame_size);

        std::mutex mutex;
        EncodedDelivery delivery;
        bool accepting = true;
    };

    struct Entry final {
        std::shared_ptr<Data> data;
        int sample_rate = 0;
        int channels = 0;
        int bits = 0;
    };

    struct WorkerState;

public:
    OpusEncoderRuntime(
        ConstructionToken,
        Config config,
        std::shared_ptr<DeliveryChannel> delivery_channel,
        std::shared_ptr<WorkerState> worker_state);

private:
    void StartWorker();
    static void WorkerMain(
        const std::shared_ptr<WorkerState>& state,
        std::stop_token stop_token);
    static void ProcessEntry(
        const std::shared_ptr<WorkerState>& state,
        const Entry& entry);

    static constexpr size_t kMaxQueue = 64;
    const Config config_;
    std::shared_ptr<DeliveryChannel> delivery_channel_;
    std::shared_ptr<WorkerState> worker_state_;
    std::jthread worker_;
    std::atomic<bool> accepting_ = true;
    std::atomic<uint64_t> dropped_ = 0;
    std::mutex shutdown_mutex_;
};

}  // namespace px
