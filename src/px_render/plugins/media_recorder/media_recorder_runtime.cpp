#include "media_recorder_runtime.h"

#include <exception>
#include <span>
#include <utility>

#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_media_record_new/record_writer.h"

namespace px {
namespace {

class RecordWriterAdapter final : public MediaRecordWriter {
public:
    explicit RecordWriterAdapter(std::shared_ptr<RecordWriter> writer)
        : writer_(std::move(writer)) {}

    void OnVideo(
        const std::shared_ptr<Data>& data,
        PxPluginEncodedVideoType video_type,
        int width,
        int height,
        bool key) override {
        if (!writer_ || !data || data->Size() <= 0) {
            return;
        }
        const auto codec = video_type == PxPluginEncodedVideoType::kH265
            ? RecordVideoCodec::kH265
            : RecordVideoCodec::kH264;
        writer_->OnEncodedVideo(
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(data->CStr()),
                static_cast<size_t>(data->Size())),
            codec, width, height, key);
    }

    void OnAudio(const std::shared_ptr<Data>& data) override {
        if (!writer_ || !data || data->Size() <= 0) {
            return;
        }
        writer_->OnEncodedAudio(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(data->CStr()),
            static_cast<size_t>(data->Size())));
    }

    void Stop() override {
        if (writer_) {
            writer_->Stop();
        }
    }

private:
    std::shared_ptr<RecordWriter> writer_;
};

}  // namespace

void MediaRecorderRuntime::KeyframeChannel::Set(KeyframeRequester callback) {
    std::lock_guard lock(mutex);
    if (accepting.load()) {
        requester = std::move(callback);
    }
}

void MediaRecorderRuntime::KeyframeChannel::Clear() {
    std::lock_guard lock(mutex);
    requester = {};
}

void MediaRecorderRuntime::KeyframeChannel::Disable() {
    accepting = false;
    Clear();
}

void MediaRecorderRuntime::KeyframeChannel::Request() {
    KeyframeRequester callback;
    {
        std::lock_guard lock(mutex);
        if (!accepting.load()) {
            return;
        }
        callback = requester;
    }
    if (callback) {
        callback();
    }
}

std::shared_ptr<MediaRecorderRuntime> MediaRecorderRuntime::Make(
    Config config,
    WriterFactory writer_factory) {
    if (!writer_factory) {
        writer_factory = DefaultWriterFactory();
    }
    auto runtime = std::make_shared<MediaRecorderRuntime>(
        ConstructionToken{}, std::move(config), std::move(writer_factory));
    runtime->StartWorker();
    return runtime;
}

MediaRecorderRuntime::MediaRecorderRuntime(
    ConstructionToken,
    Config config,
    WriterFactory writer_factory)
    : config_(std::move(config)),
      writer_factory_(std::move(writer_factory)),
      worker_state_(std::make_shared<WorkerState>()) {
    worker_state_->config = config_;
    worker_state_->writer_factory = writer_factory_;
    worker_state_->keyframe_channel = keyframe_channel_;
}

MediaRecorderRuntime::~MediaRecorderRuntime() {
    Shutdown();
}

void MediaRecorderRuntime::StartWorker() {
    const auto state = worker_state_;
    worker_ = std::jthread([state](std::stop_token stop_token) {
        WorkerMain(state, stop_token);
    });
}

void MediaRecorderRuntime::SetKeyframeRequester(KeyframeRequester requester) {
    keyframe_channel_->Set(std::move(requester));
}

void MediaRecorderRuntime::ClearKeyframeRequester() {
    keyframe_channel_->Clear();
}

void MediaRecorderRuntime::On1Second() {
    if (dropped_frames_.load() == 0) {
        return;
    }
    const auto now = TimeUtil::GetCurrentTimestamp();
    auto previous = last_drop_log_ts_.load();
    if (now - previous < 10000 ||
        !last_drop_log_ts_.compare_exchange_strong(previous, now)) {
        return;
    }
    LOGW("MediaRecord: dropped {} frames (queue full), recording: {}",
         dropped_frames_.exchange(0), recording_.load());
}

void MediaRecorderRuntime::StartRecord() {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (shutting_down_.load() || recording_.exchange(true)) {
        return;
    }
    keyframe_channel_->Request();
    LOGI("MediaRecord: recording started");
}

void MediaRecorderRuntime::StopRecord() {
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    if (!recording_.exchange(false)) {
        return;
    }

    auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();
    {
        std::lock_guard state_lock(worker_state_->mutex);
        worker_state_->queue.push_back(WorkItem{
            .type = WorkItem::Type::Finalize,
            .completion = completion,
        });
    }
    worker_state_->condition.notify_one();
    try {
        future.get();
    } catch (const std::exception& error) {
        LOGE("MediaRecord: finalize failed while stopping: {}", error.what());
    } catch (...) {
        LOGE("MediaRecord: finalize failed while stopping: unknown exception");
    }
    LOGI("MediaRecord: recording stopped");
}

void MediaRecorderRuntime::OnClientConnected(
    const std::string& visitor_device_id,
    const std::string& stream_id) {
    if (!config_.auto_enabled || shutting_down_.load()) {
        return;
    }
    const auto key = visitor_device_id.empty() ? stream_id : visitor_device_id;
    bool first = false;
    {
        std::lock_guard clients_lock(clients_mutex_);
        first = client_devices_.empty();
        client_devices_.insert(key);
    }
    if (first) {
        LOGI("MediaRecord: auto start (client connected, key={})", key);
        StartRecord();
    }
}

void MediaRecorderRuntime::OnClientDisconnected(
    const std::string& visitor_device_id,
    const std::string& stream_id) {
    if (!config_.auto_enabled || shutting_down_.load()) {
        return;
    }
    const auto key = visitor_device_id.empty() ? stream_id : visitor_device_id;
    bool last = false;
    {
        std::lock_guard clients_lock(clients_mutex_);
        client_devices_.erase(key);
        last = client_devices_.empty();
    }
    if (last) {
        LOGI("MediaRecord: auto stop (no clients, key={})", key);
        StopRecord();
    }
}

void MediaRecorderRuntime::EnqueueVideo(
    const std::string& monitor_name,
    PxPluginEncodedVideoType video_type,
    const std::shared_ptr<Data>& data,
    uint64_t frame_index,
    int width,
    int height,
    bool key) {
    if (video_type != PxPluginEncodedVideoType::kH264 &&
        video_type != PxPluginEncodedVideoType::kH265) {
        return;
    }
    Enqueue(Entry{
        .is_video = true,
        .data = data,
        .monitor_name = monitor_name,
        .video_type = video_type,
        .frame_index = frame_index,
        .width = width,
        .height = height,
        .key = key,
    });
}

void MediaRecorderRuntime::EnqueueAudio(const std::shared_ptr<Data>& data) {
    Enqueue(Entry{.is_video = false, .data = data});
}

void MediaRecorderRuntime::Enqueue(Entry entry) {
    if (!recording_.load() || !entry.data || entry.data->Size() <= 0) {
        return;
    }
    {
        std::lock_guard state_lock(worker_state_->mutex);
        if (!recording_.load() || worker_state_->shutting_down) {
            return;
        }
        if (worker_state_->queue.size() >= kMaxQueuedEntries) {
            ++dropped_frames_;
            return;
        }
        worker_state_->queue.push_back(WorkItem{
            .type = WorkItem::Type::Media,
            .entry = std::move(entry),
        });
    }
    worker_state_->condition.notify_one();
}

bool MediaRecorderRuntime::IsRecording() const {
    return recording_.load();
}

bool MediaRecorderRuntime::IsAutoEnabled() const {
    return config_.auto_enabled;
}

void MediaRecorderRuntime::Shutdown() {
    std::unique_lock lifecycle_lock(lifecycle_mutex_);
    if (shutting_down_.exchange(true)) {
        return;
    }

    if (recording_.exchange(false)) {
        auto completion = std::make_shared<std::promise<void>>();
        auto future = completion->get_future();
        {
            std::lock_guard state_lock(worker_state_->mutex);
            worker_state_->queue.push_back(WorkItem{
                .type = WorkItem::Type::Finalize,
                .completion = completion,
            });
        }
        worker_state_->condition.notify_one();
        try {
            future.get();
        } catch (const std::exception& error) {
            LOGE("MediaRecord: finalize failed during shutdown: {}", error.what());
        } catch (...) {
            LOGE("MediaRecord: finalize failed during shutdown: unknown exception");
        }
    }

    keyframe_channel_->Disable();
    {
        std::lock_guard state_lock(worker_state_->mutex);
        worker_state_->shutting_down = true;
    }
    worker_state_->condition.notify_all();
    worker_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard clients_lock(clients_mutex_);
        client_devices_.clear();
    }
}

void MediaRecorderRuntime::WorkerMain(
    const std::shared_ptr<WorkerState>& state,
    std::stop_token stop_token) {
    while (true) {
        WorkItem work;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, stop_token, [&state] {
                return state->shutting_down || !state->queue.empty();
            });
            if (state->queue.empty()) {
                if (state->shutting_down || stop_token.stop_requested()) {
                    break;
                }
                continue;
            }
            work = std::move(state->queue.front());
            state->queue.pop_front();
        }

        try {
            if (work.type == WorkItem::Type::Media) {
                ProcessEntry(state, work.entry);
            } else {
                FinalizeWriters(state);
            }
            if (work.completion) {
                work.completion->set_value();
            }
        } catch (...) {
            if (work.completion) {
                work.completion->set_exception(std::current_exception());
            } else {
                LOGE("MediaRecord: worker discarded an entry after an exception");
            }
        }
    }
    FinalizeWriters(state);
}

void MediaRecorderRuntime::ProcessEntry(
    const std::shared_ptr<WorkerState>& state,
    const Entry& entry) {
    if (!entry.data || entry.data->Size() <= 0) {
        return;
    }
    if (entry.is_video) {
        auto& writer = state->writers[entry.monitor_name];
        if (!writer) {
            const auto weak_channel = std::weak_ptr<KeyframeChannel>(
                state->keyframe_channel);
            writer = state->writer_factory(
                entry.monitor_name, state->config, [weak_channel] {
                    if (const auto channel = weak_channel.lock()) {
                        channel->Request();
                    }
                });
            LOGI("MediaRecord: writer created for monitor '{}'", entry.monitor_name);
        }
        if (writer) {
            writer->OnVideo(
                entry.data, entry.video_type, entry.width, entry.height, entry.key);
        }
        return;
    }

    for (const auto& [monitor_name, writer] : state->writers) {
        (void)monitor_name;
        if (writer) {
            writer->OnAudio(entry.data);
        }
    }
}

void MediaRecorderRuntime::FinalizeWriters(
    const std::shared_ptr<WorkerState>& state) {
    for (const auto& [monitor_name, writer] : state->writers) {
        (void)monitor_name;
        if (writer) {
            writer->Stop();
        }
    }
    state->writers.clear();
}

MediaRecorderRuntime::WriterFactory MediaRecorderRuntime::DefaultWriterFactory() {
    return [](
        const std::string& monitor_name,
        const Config& config,
        const KeyframeRequester& request_keyframe) {
        RecordWriterConfig writer_config;
        writer_config.dir = config.record_dir;
        writer_config.monitor_name = monitor_name;
        writer_config.max_segment_bytes = config.max_segment_bytes;
        writer_config.max_file_count = config.max_file_count;
        writer_config.on_request_keyframe = request_keyframe;
        return std::make_shared<RecordWriterAdapter>(
            RecordWriter::Make(writer_config));
    };
}

}  // namespace px
