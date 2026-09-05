#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "modules/builtin_module_catalog.h"
#include "pipeline/encoded_media_bus.h"
#include "px_common_new/async_result.h"

namespace px::render {

inline constexpr std::string_view kLivePusherModuleId = "f158b253-40a9-4a4a-8fb7-2b595d9f4f6f";

struct LivePusherOptions final {
    bool enabled{false};
    std::string publish_url;
    std::string primary_monitor;
    int audio_bitrate{96000};
    std::size_t queue_capacity{48};
};

struct LivePusherSnapshot final {
    bool running{false};
    bool enabled{false};
    bool publishing{false};
    std::size_t queue_depth{0};
    std::size_t queue_high_watermark{0};
    std::uint64_t accepted_media{0};
    std::uint64_t dropped_media{0};
    std::uint64_t processor_failures{0};
};

class LivePushProcessor {
  public:
    virtual ~LivePushProcessor() = default;
    virtual void ProcessVideo(const std::shared_ptr<const EncodedVideoFrame>& frame) = 0;
    virtual void ProcessAudio(const std::shared_ptr<const CapturedAudioFrame>& frame) = 0;
    [[nodiscard]] virtual bool IsPublishing() const = 0;
    virtual void Close() = 0;
};

class LivePusherSink final : public std::enable_shared_from_this<LivePusherSink> {
  public:
    using KeyframeRequester = std::function<void()>;
    using Completion = std::function<void(PxResult<void>)>;
    using ProcessorFactory =
        std::function<std::shared_ptr<LivePushProcessor>(const LivePusherOptions& options, const KeyframeRequester& request_keyframe)>;

    [[nodiscard]] static std::shared_ptr<LivePusherSink> Create(std::shared_ptr<EncodedMediaBus> media_bus, LivePusherOptions options,
                                                                KeyframeRequester request_keyframe, ProcessorFactory processor_factory);

    LivePusherSink(std::shared_ptr<EncodedMediaBus> media_bus, LivePusherOptions options, KeyframeRequester request_keyframe,
                   ProcessorFactory processor_factory);
    ~LivePusherSink();

    LivePusherSink(const LivePusherSink&) = delete;
    LivePusherSink& operator=(const LivePusherSink&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] static PxAwaitable<ModuleLifecycleResult> StopAsync(std::shared_ptr<LivePusherSink> owner,
                                                                      std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);
    void ReportPerformance();
    [[nodiscard]] LivePusherSnapshot Snapshot() const;

  private:
    enum class WorkType { kVideo, kAudio, kClose, kShutdown };
    struct WorkItem final {
        WorkType type{WorkType::kClose};
        std::shared_ptr<const EncodedVideoFrame> video;
        std::shared_ptr<const CapturedAudioFrame> audio;
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
        std::shared_ptr<LivePushProcessor> processor;
        std::atomic_bool publishing{false};
        bool shutdown_requested{false};
        std::size_t high_watermark{0};
        std::uint64_t accepted_media{0};
        std::uint64_t dropped_media{0};
        std::uint64_t processor_failures{0};
    };

    void ActivateSubscriptions();
    void DeactivateSubscriptions();
    void EnqueueVideo(std::shared_ptr<const EncodedVideoFrame> frame);
    void EnqueueAudio(std::shared_ptr<const CapturedAudioFrame> frame);
    void EnqueueMedia(WorkItem work);
    [[nodiscard]] bool SelectMonitor(const std::string& monitor_id);
    [[nodiscard]] bool RequestControl(WorkType type, Completion completion);
    void JoinWorker();
    void ShutdownForDestruction();
    static void WorkerMain(const std::shared_ptr<WorkerState>& state);

    const std::shared_ptr<EncodedMediaBus> media_bus_;
    const LivePusherOptions options_;
    const ProcessorFactory processor_factory_;
    const std::shared_ptr<KeyframeChannel> keyframe_channel_;

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex monitor_mutex_;
    std::shared_ptr<WorkerState> worker_state_;
    std::jthread worker_;
    std::shared_ptr<EncodedMediaBus::VideoCallback> video_callback_;
    std::shared_ptr<EncodedMediaBus::CapturedAudioCallback> audio_callback_;
    std::shared_ptr<ScopedSubscription> video_subscription_;
    std::shared_ptr<ScopedSubscription> audio_subscription_;
    std::string selected_monitor_;
    bool monitor_selected_{false};
    std::atomic_bool running_{false};
    std::atomic_bool enabled_{false};
};

[[nodiscard]] std::string BuildLivePublishUrl(std::string url, const std::string& live_stream_id);

} // namespace px::render
