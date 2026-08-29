#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {

class Data;

class LivePushProcessor {
public:
    virtual ~LivePushProcessor() = default;
    virtual void ProcessVideo(
        const std::shared_ptr<Data>& data,
        PxPluginEncodedVideoType video_type,
        int width,
        int height,
        bool key,
        int64_t timestamp_ms) = 0;
    virtual void ProcessAudio(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits,
        int64_t timestamp_ms) = 0;
    [[nodiscard]] virtual bool IsPublishing() const = 0;
    virtual void Close() = 0;
};

class LivePusherRuntime final {
public:
    struct Config final {
        std::string publish_url;
        std::string primary_monitor;
        int audio_bitrate = 96000;
    };

    using KeyframeRequester = std::function<void()>;
    using ProcessorFactory = std::function<std::shared_ptr<LivePushProcessor>(
        const Config& config,
        const KeyframeRequester& request_keyframe)>;

    static std::shared_ptr<LivePusherRuntime> Make(
        Config config,
        ProcessorFactory processor_factory);
    ~LivePusherRuntime();

    LivePusherRuntime(const LivePusherRuntime&) = delete;
    LivePusherRuntime& operator=(const LivePusherRuntime&) = delete;

    void SetKeyframeRequester(KeyframeRequester requester);
    void ClearKeyframeRequester();
    void EnqueueVideo(
        const std::string& monitor_name,
        PxPluginEncodedVideoType video_type,
        const std::shared_ptr<Data>& data,
        int width,
        int height,
        bool key,
        int64_t timestamp_ms);
    void EnqueueAudio(
        const std::shared_ptr<Data>& data,
        int sample_rate,
        int channels,
        int bits,
        int64_t timestamp_ms);
    void On1Second(int64_t now_ms);
    [[nodiscard]] bool IsAccepting() const;
    void Shutdown();

private:
    struct ConstructionToken final {};
    struct KeyframeChannel final {
        void Set(KeyframeRequester requester);
        void Clear();
        void Disable();
        void Request();

        std::mutex mutex;
        KeyframeRequester requester;
        bool accepting = true;
    };

    struct Entry final {
        enum class Kind { Video, Audio };
        Kind kind = Kind::Video;
        std::shared_ptr<Data> data;
        PxPluginEncodedVideoType video_type = PxPluginEncodedVideoType::kH264;
        int width = 0;
        int height = 0;
        bool key = false;
        int sample_rate = 0;
        int channels = 0;
        int bits = 0;
        int64_t timestamp_ms = 0;
    };

    struct WorkerState final {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::deque<Entry> queue;
        std::shared_ptr<LivePushProcessor> processor;
        bool shutting_down = false;
    };

public:
    LivePusherRuntime(
        ConstructionToken,
        Config config,
        std::shared_ptr<KeyframeChannel> keyframe_channel,
        std::shared_ptr<WorkerState> worker_state);

private:
    void StartWorker();
    void Enqueue(Entry entry);
    bool IsSelectedMonitor(const std::string& monitor_name);
    static void WorkerMain(
        const std::shared_ptr<WorkerState>& state,
        std::stop_token stop_token);

    static constexpr size_t kMaxQueue = 48;
    const Config config_;
    std::shared_ptr<KeyframeChannel> keyframe_channel_;
    std::shared_ptr<WorkerState> worker_state_;
    std::jthread worker_;
    std::atomic<bool> accepting_ = true;
    std::atomic<uint64_t> dropped_ = 0;
    std::atomic<int64_t> last_drop_log_ms_ = 0;
    std::mutex monitor_mutex_;
    std::string selected_monitor_;
    bool monitor_selected_ = false;
    std::mutex shutdown_mutex_;
};

}  // namespace px
