#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "modules/builtin_module_catalog.h"
#include "pipeline/encoded_media_bus.h"
#include "px_common_new/async_result.h"

namespace px::render {

inline constexpr std::string_view kMediaRecorderModuleId =
    "21d1c305-e68c-4079-8a4a-d00735be609b";

struct MediaRecorderOptions final {
    std::string record_directory;
    bool auto_enabled{false};
    std::int64_t max_segment_bytes{1024LL * 1024 * 1024};
    int max_file_count{24};
    std::size_t queue_capacity{512};
};

struct MediaRecorderSnapshot final {
    bool running{false};
    bool enabled{false};
    bool recording{false};
    std::size_t queue_depth{0};
    std::size_t queue_high_watermark{0};
    std::uint64_t accepted_media{0};
    std::uint64_t dropped_media{0};
    std::uint64_t video_packets_written{0};
    std::uint64_t audio_packets_written{0};
    std::uint64_t writer_failures{0};
};

class MediaRecorderWriter {
public:
    virtual ~MediaRecorderWriter() = default;
    virtual void OnVideo(
        const std::shared_ptr<const EncodedVideoFrame>& frame) = 0;
    virtual void OnAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame) = 0;
    virtual void Stop() = 0;
};

class MediaRecorderSink final
    : public std::enable_shared_from_this<MediaRecorderSink> {
public:
    using KeyframeRequester = std::function<void()>;
    using Completion = std::function<void(PxResult<void>)>;
    using WriterFactory = std::function<std::shared_ptr<MediaRecorderWriter>(
        const std::string& monitor_id,
        const MediaRecorderOptions& options,
        const KeyframeRequester& request_keyframe)>;

    [[nodiscard]] static std::shared_ptr<MediaRecorderSink> Create(
        std::shared_ptr<EncodedMediaBus> media_bus,
        MediaRecorderOptions options,
        KeyframeRequester request_keyframe,
        WriterFactory writer_factory = {});

    MediaRecorderSink(std::shared_ptr<EncodedMediaBus> media_bus,
                      MediaRecorderOptions options,
                      KeyframeRequester request_keyframe,
                      WriterFactory writer_factory);
    ~MediaRecorderSink();

    MediaRecorderSink(const MediaRecorderSink&) = delete;
    MediaRecorderSink& operator=(const MediaRecorderSink&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] static PxAwaitable<ModuleLifecycleResult> StopAsync(
        const std::shared_ptr<MediaRecorderSink>& owner,
        std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void StartRecording();
    void StopRecording();
    void ReportPerformance();
    [[nodiscard]] bool IsAutoEnabled() const noexcept;
    [[nodiscard]] MediaRecorderSnapshot Snapshot() const;

private:
    enum class WorkType { kVideo, kAudio, kFinalize, kShutdown };

    struct WorkItem final {
        WorkType type{WorkType::kFinalize};
        std::shared_ptr<const EncodedVideoFrame> video;
        std::shared_ptr<const EncodedAudioFrame> audio;
        Completion completion;
    };

    struct KeyframeChannel final {
        void Request() const;
        void Disable();

        mutable std::mutex mutex;
        KeyframeRequester requester;
        bool accepting{true};
    };

    struct WorkerState final {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<WorkItem> queue;
        std::map<std::string, std::shared_ptr<MediaRecorderWriter>> writers;
        MediaRecorderOptions options;
        WriterFactory writer_factory;
        std::shared_ptr<KeyframeChannel> keyframe_channel;
        bool shutdown_requested{false};
        std::size_t high_watermark{0};
        std::uint64_t accepted_media{0};
        std::uint64_t dropped_media{0};
        std::uint64_t video_packets_written{0};
        std::uint64_t audio_packets_written{0};
        std::uint64_t writer_failures{0};
    };

    void ActivateMediaSubscriptions();
    void DeactivateMediaSubscriptions();
    void ActivateClientSubscriptions();
    void DeactivateClientSubscriptions();
    void OnClientConnected(const MediaClientConnected& event);
    void OnClientDisconnected(const MediaClientDisconnected& event);
    void EnqueueVideo(std::shared_ptr<const EncodedVideoFrame> frame);
    void EnqueueAudio(std::shared_ptr<const EncodedAudioFrame> frame);
    void EnqueueMedia(WorkItem work);
    [[nodiscard]] bool RequestFinalize(Completion completion);
    [[nodiscard]] bool RequestShutdown(Completion completion);
    void JoinWorker();
    void ShutdownForDestruction();

    static void WorkerMain(const std::shared_ptr<WorkerState>& state);
    static void ProcessVideo(const std::shared_ptr<WorkerState>& state,
                             const std::shared_ptr<const EncodedVideoFrame>& frame);
    static void ProcessAudio(const std::shared_ptr<WorkerState>& state,
                             const std::shared_ptr<const EncodedAudioFrame>& frame);
    static void FinalizeWriters(const std::shared_ptr<WorkerState>& state);
    [[nodiscard]] static WriterFactory DefaultWriterFactory();

    const std::shared_ptr<EncodedMediaBus> media_bus_;
    const MediaRecorderOptions options_;
    const WriterFactory writer_factory_;
    const std::shared_ptr<KeyframeChannel> keyframe_channel_;

    mutable std::mutex lifecycle_mutex_;
    std::shared_ptr<WorkerState> worker_state_;
    std::jthread worker_;
    std::shared_ptr<EncodedMediaBus::VideoCallback> video_callback_;
    std::shared_ptr<EncodedMediaBus::EncodedAudioCallback> audio_callback_;
    std::shared_ptr<EncodedMediaBus::ClientConnectedCallback>
        client_connected_callback_;
    std::shared_ptr<EncodedMediaBus::ClientDisconnectedCallback>
        client_disconnected_callback_;
    std::shared_ptr<ScopedSubscription> video_subscription_;
    std::shared_ptr<ScopedSubscription> audio_subscription_;
    std::shared_ptr<ScopedSubscription> client_connected_subscription_;
    std::shared_ptr<ScopedSubscription> client_disconnected_subscription_;
    std::set<std::string> connected_clients_;
    std::atomic_bool running_{false};
    std::atomic_bool enabled_{true};
    std::atomic_bool recording_{false};
};

}  // namespace px::render
