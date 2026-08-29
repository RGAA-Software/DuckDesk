#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <thread>

#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {

class Data;

class MediaRecordWriter {
public:
    virtual ~MediaRecordWriter() = default;
    virtual void OnVideo(
        const std::shared_ptr<Data>& data,
        PxPluginEncodedVideoType video_type,
        int width,
        int height,
        bool key) = 0;
    virtual void OnAudio(const std::shared_ptr<Data>& data) = 0;
    virtual void Stop() = 0;
};

class MediaRecorderRuntime final {
public:
    struct Config final {
        std::string record_dir;
        bool auto_enabled = false;
        int64_t max_segment_bytes = 1024LL * 1024 * 1024;
        int max_file_count = 24;
    };

    using KeyframeRequester = std::function<void()>;
    using WriterFactory = std::function<std::shared_ptr<MediaRecordWriter>(
        const std::string& monitor_name,
        const Config& config,
        const KeyframeRequester& request_keyframe)>;

    static std::shared_ptr<MediaRecorderRuntime> Make(
        Config config,
        WriterFactory writer_factory = {});
    ~MediaRecorderRuntime();

    MediaRecorderRuntime(const MediaRecorderRuntime&) = delete;
    MediaRecorderRuntime& operator=(const MediaRecorderRuntime&) = delete;

    void SetKeyframeRequester(KeyframeRequester requester);
    void ClearKeyframeRequester();
    void On1Second();
    void StartRecord();
    void StopRecord();
    void OnClientConnected(
        const std::string& visitor_device_id,
        const std::string& stream_id);
    void OnClientDisconnected(
        const std::string& visitor_device_id,
        const std::string& stream_id);
    void EnqueueVideo(
        const std::string& monitor_name,
        PxPluginEncodedVideoType video_type,
        const std::shared_ptr<Data>& data,
        uint64_t frame_index,
        int width,
        int height,
        bool key);
    void EnqueueAudio(const std::shared_ptr<Data>& data);
    [[nodiscard]] bool IsRecording() const;
    [[nodiscard]] bool IsAutoEnabled() const;
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
        std::atomic<bool> accepting = true;
    };

    struct Entry final {
        bool is_video = false;
        std::shared_ptr<Data> data;
        std::string monitor_name;
        PxPluginEncodedVideoType video_type = PxPluginEncodedVideoType::kH264;
        uint64_t frame_index = 0;
        int width = 0;
        int height = 0;
        bool key = false;
    };

    struct WorkItem final {
        enum class Type { Media, Finalize };
        Type type = Type::Media;
        Entry entry;
        std::shared_ptr<std::promise<void>> completion;
    };

    struct WorkerState final {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::deque<WorkItem> queue;
        std::map<std::string, std::shared_ptr<MediaRecordWriter>> writers;
        Config config;
        WriterFactory writer_factory;
        std::shared_ptr<KeyframeChannel> keyframe_channel;
        bool shutting_down = false;
    };

public:
    MediaRecorderRuntime(
        ConstructionToken,
        Config config,
        WriterFactory writer_factory);

private:
    void StartWorker();
    void Enqueue(Entry entry);
    static void WorkerMain(
        const std::shared_ptr<WorkerState>& state,
        std::stop_token stop_token);
    static void ProcessEntry(
        const std::shared_ptr<WorkerState>& state,
        const Entry& entry);
    static void FinalizeWriters(const std::shared_ptr<WorkerState>& state);
    static WriterFactory DefaultWriterFactory();

    static constexpr size_t kMaxQueuedEntries = 512;
    const Config config_;
    const WriterFactory writer_factory_;
    std::shared_ptr<KeyframeChannel> keyframe_channel_ =
        std::make_shared<KeyframeChannel>();
    std::shared_ptr<WorkerState> worker_state_;
    std::jthread worker_;
    std::atomic<bool> recording_ = false;
    std::atomic<bool> shutting_down_ = false;
    std::atomic<uint64_t> dropped_frames_ = 0;
    std::atomic<int64_t> last_drop_log_ts_ = 0;
    std::mutex lifecycle_mutex_;
    std::mutex clients_mutex_;
    std::set<std::string> client_devices_;
};

}  // namespace px
