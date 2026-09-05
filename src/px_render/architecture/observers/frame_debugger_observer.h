#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

#include "diagnostics/rate_limited_log.h"
#include "modules/builtin_module_catalog.h"
#include "pipeline/bounded_media_queue.h"
#include "pipeline/media_types.h"
#include "px_common_new/async_operation.h"
#include "px_common_new/async_runtime.h"

namespace px {
class File;
}

namespace px::render {

inline constexpr std::string_view kFrameDebuggerModuleId = "bfb3fadc-6f37-401c-a927-88c3ae2d1e95";

struct FrameDebuggerOptions final {
    std::size_t queue_capacity{120};
    bool save_encoded_video{false};
    std::filesystem::path output_directory;
    std::chrono::milliseconds raw_log_interval{std::chrono::seconds(1)};
};

struct VideoEncoderReady final {
    std::string monitor_id;
    std::string codec;
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct RawVideoFrameObservation final {
    std::string monitor_id;
    std::uint64_t frame_index{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct FrameDebuggerClientConnected final {};

using FrameDebuggerEvent = std::variant<VideoEncoderReady, std::shared_ptr<const EncodedVideoFrame>, FrameDebuggerClientConnected>;

enum class FrameDebuggerSubmitResult {
    kAccepted,
    kAcceptedAfterDroppingOldest,
    kDisabled,
    kStopped,
};

struct FrameDebuggerSnapshot final {
    bool running{false};
    bool enabled{false};
    std::uint64_t raw_frames_observed{0};
    std::uint64_t encoded_frames_submitted{0};
    std::uint64_t encoded_bytes_written{0};
    std::uint64_t file_write_failures{0};
    MediaQueueSnapshot queue;
};

// Lifetime:
// - Owned by RenderCompositionRoot through the explicit registration factory.
// - worker_scope_ owns one long-running consumer coroutine.
// - The consumer receives weak ownership and independently owned queue/
//   completion state, so queued work remains safe during owner destruction.
// - Encoded frame payloads are shared immutable values.
//
// Threading:
// - Submit methods are multi-producer and never wait for file I/O.
// - File state is confined to the worker coroutine.
// - Flags and counters are atomic; queue state is internally synchronized.
// - No lock or borrowed reference is held across co_await.
class FrameDebuggerObserver final : public std::enable_shared_from_this<FrameDebuggerObserver> {
  public:
    static std::shared_ptr<FrameDebuggerObserver> Create(const std::shared_ptr<PxAsyncRuntime>& runtime, FrameDebuggerOptions options = {});

    FrameDebuggerObserver(std::shared_ptr<PxAsyncScope> worker_scope, FrameDebuggerOptions options);
    ~FrameDebuggerObserver();

    FrameDebuggerObserver(const FrameDebuggerObserver&) = delete;
    FrameDebuggerObserver& operator=(const FrameDebuggerObserver&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] static PxAwaitable<ModuleLifecycleResult> StopAsync(std::shared_ptr<FrameDebuggerObserver> owner,
                                                                      std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    [[nodiscard]] FrameDebuggerSubmitResult SubmitEncoderReady(VideoEncoderReady event);
    [[nodiscard]] FrameDebuggerSubmitResult SubmitEncodedFrame(std::shared_ptr<const EncodedVideoFrame> frame);
    [[nodiscard]] FrameDebuggerSubmitResult SubmitClientConnected();
    void ObserveRawFrame(RawVideoFrameObservation frame);
    [[nodiscard]] bool WantsEncodedFrames() const noexcept;
    [[nodiscard]] FrameDebuggerSnapshot Snapshot() const;

  private:
    static PxAwaitable<void> ConsumeLoop(std::weak_ptr<FrameDebuggerObserver> weak_owner,
                                         std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue,
                                         std::shared_ptr<PxAsyncOneShot<void>> completion, asio::any_io_executor executor);
    void ProcessEvent(const FrameDebuggerEvent& event);
    void ProcessEncoderReady(const VideoEncoderReady& event);
    void ProcessEncodedFrame(const std::shared_ptr<const EncodedVideoFrame>& frame);
    void CloseFiles();
    [[nodiscard]] FrameDebuggerSubmitResult SubmitEvent(std::shared_ptr<const FrameDebuggerEvent> event);

    std::shared_ptr<PxAsyncScope> worker_scope_;
    const FrameDebuggerOptions options_;
    mutable std::mutex queue_mutex_;
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue_;
    std::shared_ptr<PxAsyncOneShot<void>> consumer_completion_;

    std::atomic_bool running_{false};
    std::atomic_bool enabled_{false};
    std::atomic_uint64_t raw_frames_observed_{0};
    std::atomic_uint64_t encoded_frames_submitted_{0};
    std::atomic_uint64_t encoded_bytes_written_{0};
    std::atomic_uint64_t file_write_failures_{0};
    RateLimitedLogGate raw_log_gate_;
    RateLimitedLogGate drop_log_gate_;

    // Worker-lane confined state.
    std::map<std::string, std::shared_ptr<File>> encoded_video_files_;
    bool rotate_on_next_encoder_{false};
};

} // namespace px::render
