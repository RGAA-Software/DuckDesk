#pragma once

#include "px_media_record/record_writer.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace px {
class Message;

struct RecordingSessionConfig final {
    RecordWriterConfig writer{};
    std::size_t monitor_count{1};
    std::size_t queue_byte_limit{32U * 1024U * 1024U};
    std::size_t queue_packet_limit{256};
};

struct RecordingSessionResult final {
    std::vector<std::string> directories{};
    std::string error{};
    std::uint64_t processed_packets{};
    std::uint64_t video_packets{};
    std::uint64_t audio_packets{};
    std::uint64_t audio_gap_packets{};
};

struct RecordingSessionCallbacks final {
    std::function<void()> started{};
    std::function<void(const RecordingSessionResult&)> finished{};
};

enum class RecordingSubmitResult { kAccepted, kIgnored, kStopped, kInvalid, kQueueFull };

// One object is one recording run. A stopped run never accepts work again.
// The private FIFO owns encoded messages and drains before finalizing writers.
// Callbacks run on its worker, must be bounded, and should capture weak host owners.
class RecordingSession final {
  private:
    struct ConstructionToken final {};

  public:
    static std::shared_ptr<RecordingSession> Create(RecordingSessionConfig config, RecordingSessionCallbacks callbacks = {});
    RecordingSession(ConstructionToken, RecordingSessionConfig config, RecordingSessionCallbacks callbacks);
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    [[nodiscard]] bool Start();
    [[nodiscard]] RecordingSubmitResult Submit(std::shared_ptr<const Message> message);
    void Stop(); // Reject further work and drain accepted packets; never waits on a callback.
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout) const;
    [[nodiscard]] std::shared_future<RecordingSessionResult> Completion() const;

  private:
    struct State;
    static void Run(std::shared_ptr<State> state);
    const std::shared_ptr<State> state_;
    mutable std::mutex thread_mutex_{};
    std::thread worker_{};
};
} // namespace px
