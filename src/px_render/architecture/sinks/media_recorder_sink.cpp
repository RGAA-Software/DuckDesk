#include "sinks/media_recorder_sink.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <span>
#include <utility>

#include "px_common_new/log.h"
#include "px_media_record_new/record_writer.h"
#include "runtime/await_callback.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

RenderError MakeRecorderError(const RenderErrorCode code,
                              std::string operation,
                              std::string reason,
                              const bool recoverable) {
    return RenderError{
        .code = code,
        .component = "media_recorder",
        .operation = std::move(operation),
        .stage = "sink",
        .reason = std::move(reason),
        .recoverable = recoverable,
    };
}

class RecordWriterAdapter final : public MediaRecorderWriter {
public:
    explicit RecordWriterAdapter(std::shared_ptr<RecordWriter> writer)
        : writer_(std::move(writer)) {}

    void OnVideo(
        const std::shared_ptr<const EncodedVideoFrame>& frame) override {
        if (!writer_ || !frame || !frame->payload || frame->payload->empty()) {
            return;
        }
        const auto codec = frame->codec == "h265"
            ? RecordVideoCodec::kH265
            : RecordVideoCodec::kH264;
        writer_->OnEncodedVideo(std::span<const std::uint8_t>(*frame->payload),
                                codec,
                                static_cast<int>(frame->width),
                                static_cast<int>(frame->height),
                                frame->key_frame);
    }

    void OnAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame) override {
        if (!writer_ || !frame || !frame->payload || frame->payload->empty()) {
            return;
        }
        writer_->OnEncodedAudio(std::span<const std::uint8_t>(*frame->payload));
    }

    void Stop() override {
        if (writer_) {
            writer_->Stop();
        }
    }

private:
    std::shared_ptr<RecordWriter> writer_;
};

void Complete(const MediaRecorderSink::Completion& completion,
              PxResult<void> result) {
    if (!completion) {
        return;
    }
    try {
        completion(std::move(result));
    }
    catch (const std::exception& error) {
        LOGE("event=sink.completion component=media_recorder "
             "outcome=ignored reason={}", error.what());
    }
    catch (...) {
        LOGE("event=sink.completion component=media_recorder "
             "outcome=ignored reason=unknown_exception");
    }
}

}  // namespace

std::shared_ptr<MediaRecorderSink> MediaRecorderSink::Create(
    std::shared_ptr<EncodedMediaBus> media_bus,
    MediaRecorderOptions options,
    KeyframeRequester request_keyframe,
    WriterFactory writer_factory) {
    if (!media_bus || options.queue_capacity == 0) {
        return {};
    }
    if (options.record_directory.empty()) {
        options.record_directory =
            (std::filesystem::temp_directory_path() / "px_render_records").string();
    }
    if (options.max_segment_bytes <= 0) {
        options.max_segment_bytes = 1024LL * 1024 * 1024;
    }
    if (options.max_file_count <= 0) {
        options.max_file_count = 24;
    }
    if (!writer_factory) {
        writer_factory = DefaultWriterFactory();
    }
    return std::make_shared<MediaRecorderSink>(
        std::move(media_bus),
        std::move(options),
        std::move(request_keyframe),
        std::move(writer_factory));
}

MediaRecorderSink::MediaRecorderSink(
    std::shared_ptr<EncodedMediaBus> media_bus,
    MediaRecorderOptions options,
    KeyframeRequester request_keyframe,
    WriterFactory writer_factory)
    : media_bus_(std::move(media_bus)),
      options_(std::move(options)),
      writer_factory_(std::move(writer_factory)),
      keyframe_channel_(std::make_shared<KeyframeChannel>()) {
    keyframe_channel_->requester = std::move(request_keyframe);
}

MediaRecorderSink::~MediaRecorderSink() {
    ShutdownForDestruction();
}

BuiltinModuleRegistration MediaRecorderSink::MakeRegistration() {
    const std::weak_ptr<MediaRecorderSink> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kMediaRecorderModuleId),
            .name = "Media Recorder(Server)",
            .author = "GammaRay",
            .description = "Built-in bounded encoded-media recording sink",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kSink,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            if (!owner) {
                co_return std::unexpected(MakeRecorderError(
                    RenderErrorCode::kModuleStartFailed,
                    "start",
                    "sink owner expired",
                    false));
            }
            co_return owner->Start();
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            if (!owner) {
                co_return ModuleLifecycleResult{};
            }
            co_return co_await StopAsync(
                owner, std::chrono::steady_clock::now() + 10s);
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            if (!owner) {
                return ModuleLifecycleResult(std::unexpected(
                    MakeRecorderError(
                        RenderErrorCode::kModuleLifecycleRejected,
                        "set_enabled",
                        "sink owner expired",
                        false)));
            }
            return owner->SetEnabled(enabled);
        },
    };
}

ModuleLifecycleResult MediaRecorderSink::Start() {
    std::unique_lock lock(lifecycle_mutex_);
    if (running_.load(std::memory_order_acquire)) {
        return {};
    }

    auto state = std::make_shared<WorkerState>();
    state->options = options_;
    state->writer_factory = writer_factory_;
    state->keyframe_channel = keyframe_channel_;
    worker_state_ = state;

    worker_ = std::jthread([state] { WorkerMain(state); });
    running_.store(true, std::memory_order_release);
    lock.unlock();
    if (options_.auto_enabled) {
        ActivateClientSubscriptions();
    }

    LOGI("event=sink.start component=media_recorder outcome=success "
         "queue_capacity={} auto_enabled={} max_segment_bytes={} "
         "max_file_count={} dir={}",
         options_.queue_capacity,
         options_.auto_enabled,
         options_.max_segment_bytes,
         options_.max_file_count,
         options_.record_directory);
    return {};
}

PxAwaitable<ModuleLifecycleResult> MediaRecorderSink::StopAsync(
    const std::shared_ptr<MediaRecorderSink>& owner,
    const std::chrono::steady_clock::time_point deadline) {
    owner->recording_.store(false, std::memory_order_release);
    owner->running_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(owner->lifecycle_mutex_);
        owner->connected_clients_.clear();
    }
    owner->DeactivateMediaSubscriptions();
    owner->DeactivateClientSubscriptions();

    const std::weak_ptr<MediaRecorderSink> weak_owner = owner;
    const auto stopped = co_await AwaitOwnedCallback<void>(
        [weak_owner](Completion completion) {
            const auto active_owner = weak_owner.lock();
            return active_owner &&
                   active_owner->RequestShutdown(std::move(completion));
        },
        deadline,
        "media_recorder.stop");
    if (!stopped) {
        co_return std::unexpected(MakeRecorderError(
            RenderErrorCode::kAsyncScopeDrainTimeout,
            "stop",
            stopped.Error().message,
            true));
    }
    owner->JoinWorker();
    const auto snapshot = owner->Snapshot();
    LOGI("event=sink.stop component=media_recorder outcome=success "
         "accepted={} dropped={} high_watermark={} video_packets={} "
         "audio_packets={} writer_failures={}",
         snapshot.accepted_media,
         snapshot.dropped_media,
         snapshot.queue_high_watermark,
         snapshot.video_packets_written,
         snapshot.audio_packets_written,
         snapshot.writer_failures);
    co_return ModuleLifecycleResult{};
}

ModuleLifecycleResult MediaRecorderSink::SetEnabled(const bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled && recording_.exchange(false, std::memory_order_acq_rel)) {
        DeactivateMediaSubscriptions();
        static_cast<void>(RequestFinalize({}));
    }
    if (enabled && options_.auto_enabled) {
        ActivateClientSubscriptions();
    }
    else if (!enabled) {
        DeactivateClientSubscriptions();
    }
    return {};
}

void MediaRecorderSink::StartRecording() {
    if (!running_.load(std::memory_order_acquire) ||
        !enabled_.load(std::memory_order_acquire)) {
        return;
    }
    if (options_.auto_enabled) {
        LOGI("event=record.command component=media_recorder action=start "
             "outcome=ignored reason=auto_mode");
        return;
    }
    if (!recording_.exchange(true, std::memory_order_acq_rel)) {
        ActivateMediaSubscriptions();
        keyframe_channel_->Request();
        LOGI("event=record.session component=media_recorder action=start "
             "outcome=success");
    }
}

void MediaRecorderSink::StopRecording() {
    if (options_.auto_enabled) {
        LOGI("event=record.command component=media_recorder action=stop "
             "outcome=ignored reason=auto_mode");
        return;
    }
    if (recording_.exchange(false, std::memory_order_acq_rel)) {
        DeactivateMediaSubscriptions();
        static_cast<void>(RequestFinalize({}));
        LOGI("event=record.session component=media_recorder action=stop "
             "outcome=accepted");
    }
}

void MediaRecorderSink::ReportPerformance() {
    const auto snapshot = Snapshot();
    if (snapshot.dropped_media == 0 && !snapshot.recording) {
        return;
    }
    LOGI("event=sink.performance component=media_recorder recording={} "
         "queue_depth={} high_watermark={} accepted={} dropped={} "
         "video_packets={} audio_packets={} writer_failures={}",
         snapshot.recording,
         snapshot.queue_depth,
         snapshot.queue_high_watermark,
         snapshot.accepted_media,
         snapshot.dropped_media,
         snapshot.video_packets_written,
         snapshot.audio_packets_written,
         snapshot.writer_failures);
}

bool MediaRecorderSink::IsAutoEnabled() const noexcept {
    return options_.auto_enabled;
}

MediaRecorderSnapshot MediaRecorderSink::Snapshot() const {
    auto snapshot = MediaRecorderSnapshot{
        .running = running_.load(std::memory_order_acquire),
        .enabled = enabled_.load(std::memory_order_acquire),
        .recording = recording_.load(std::memory_order_acquire),
    };
    const auto state = worker_state_;
    if (!state) {
        return snapshot;
    }
    std::lock_guard lock(state->mutex);
    snapshot.queue_depth = state->queue.size();
    snapshot.queue_high_watermark = state->high_watermark;
    snapshot.accepted_media = state->accepted_media;
    snapshot.dropped_media = state->dropped_media;
    snapshot.video_packets_written = state->video_packets_written;
    snapshot.audio_packets_written = state->audio_packets_written;
    snapshot.writer_failures = state->writer_failures;
    return snapshot;
}

void MediaRecorderSink::ActivateMediaSubscriptions() {
    std::lock_guard lock(lifecycle_mutex_);
    if (video_subscription_ || audio_subscription_ ||
        !running_.load(std::memory_order_acquire)) {
        return;
    }
    const std::weak_ptr<MediaRecorderSink> weak_owner = weak_from_this();
    video_callback_ = std::make_shared<EncodedMediaBus::VideoCallback>(
        [weak_owner](const std::shared_ptr<const EncodedVideoFrame>& frame) {
            const auto owner = weak_owner.lock();
            if (owner) {
                owner->EnqueueVideo(frame);
            }
        });
    audio_callback_ = std::make_shared<EncodedMediaBus::EncodedAudioCallback>(
        [weak_owner](const std::shared_ptr<const EncodedAudioFrame>& frame) {
            const auto owner = weak_owner.lock();
            if (owner) {
                owner->EnqueueAudio(frame);
            }
        });
    video_subscription_ = media_bus_->SubscribeVideo(video_callback_);
    audio_subscription_ = media_bus_->SubscribeEncodedAudio(audio_callback_);
}

void MediaRecorderSink::DeactivateMediaSubscriptions() {
    std::shared_ptr<ScopedSubscription> video;
    std::shared_ptr<ScopedSubscription> audio;
    {
        std::lock_guard lock(lifecycle_mutex_);
        video = std::move(video_subscription_);
        audio = std::move(audio_subscription_);
        video_callback_.reset();
        audio_callback_.reset();
    }
    if (video) {
        video->Reset();
    }
    if (audio) {
        audio->Reset();
    }
}

void MediaRecorderSink::ActivateClientSubscriptions() {
    std::lock_guard lock(lifecycle_mutex_);
    if (client_connected_subscription_ || client_disconnected_subscription_ ||
        !running_.load(std::memory_order_acquire)) {
        return;
    }
    const std::weak_ptr<MediaRecorderSink> weak_owner = weak_from_this();
    client_connected_callback_ =
        std::make_shared<EncodedMediaBus::ClientConnectedCallback>(
            [weak_owner](const MediaClientConnected& event) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnClientConnected(event);
                }
            });
    client_disconnected_callback_ =
        std::make_shared<EncodedMediaBus::ClientDisconnectedCallback>(
            [weak_owner](const MediaClientDisconnected& event) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnClientDisconnected(event);
                }
            });
    client_connected_subscription_ =
        media_bus_->SubscribeClientConnected(client_connected_callback_);
    client_disconnected_subscription_ =
        media_bus_->SubscribeClientDisconnected(client_disconnected_callback_);
}

void MediaRecorderSink::DeactivateClientSubscriptions() {
    std::shared_ptr<ScopedSubscription> connected;
    std::shared_ptr<ScopedSubscription> disconnected;
    {
        std::lock_guard lock(lifecycle_mutex_);
        connected = std::move(client_connected_subscription_);
        disconnected = std::move(client_disconnected_subscription_);
        client_connected_callback_.reset();
        client_disconnected_callback_.reset();
    }
    if (connected) {
        connected->Reset();
    }
    if (disconnected) {
        disconnected->Reset();
    }
}

void MediaRecorderSink::OnClientConnected(const MediaClientConnected& event) {
    if (!options_.auto_enabled || !enabled_.load(std::memory_order_acquire)) {
        return;
    }
    const auto key = event.visitor_device_id.empty()
        ? event.stream_id
        : event.visitor_device_id;
    bool start = false;
    {
        std::lock_guard lock(lifecycle_mutex_);
        start = connected_clients_.empty();
        connected_clients_.insert(key);
    }
    if (start && !recording_.exchange(true, std::memory_order_acq_rel)) {
        ActivateMediaSubscriptions();
        keyframe_channel_->Request();
        LOGI("event=record.session component=media_recorder action=auto_start "
             "outcome=success transport={}", event.transport);
    }
}

void MediaRecorderSink::OnClientDisconnected(
    const MediaClientDisconnected& event) {
    if (!options_.auto_enabled) {
        return;
    }
    const auto key = event.visitor_device_id.empty()
        ? event.stream_id
        : event.visitor_device_id;
    bool stop = false;
    {
        std::lock_guard lock(lifecycle_mutex_);
        connected_clients_.erase(key);
        stop = connected_clients_.empty();
    }
    if (stop && recording_.exchange(false, std::memory_order_acq_rel)) {
        DeactivateMediaSubscriptions();
        static_cast<void>(RequestFinalize({}));
        LOGI("event=record.session component=media_recorder action=auto_stop "
             "outcome=accepted transport={}", event.transport);
    }
}

void MediaRecorderSink::EnqueueVideo(
    std::shared_ptr<const EncodedVideoFrame> frame) {
    if (!frame || (frame->codec != "h264" && frame->codec != "h265")) {
        return;
    }
    EnqueueMedia(WorkItem{.type = WorkType::kVideo, .video = std::move(frame)});
}

void MediaRecorderSink::EnqueueAudio(
    std::shared_ptr<const EncodedAudioFrame> frame) {
    if (!frame || frame->codec != "opus") {
        return;
    }
    EnqueueMedia(WorkItem{.type = WorkType::kAudio, .audio = std::move(frame)});
}

void MediaRecorderSink::EnqueueMedia(WorkItem work) {
    if (!running_.load(std::memory_order_acquire) ||
        !enabled_.load(std::memory_order_acquire) ||
        !recording_.load(std::memory_order_acquire)) {
        return;
    }
    const auto state = worker_state_;
    if (!state) {
        return;
    }
    {
        std::lock_guard lock(state->mutex);
        if (state->shutdown_requested ||
            !recording_.load(std::memory_order_acquire)) {
            return;
        }
        if (state->queue.size() >= options_.queue_capacity) {
            ++state->dropped_media;
            return;
        }
        state->queue.push_back(std::move(work));
        ++state->accepted_media;
        state->high_watermark =
            std::max(state->high_watermark, state->queue.size());
    }
    state->condition.notify_one();
}

bool MediaRecorderSink::RequestFinalize(Completion completion) {
    const auto state = worker_state_;
    if (!state) {
        Complete(completion, PxResult<void>::Success());
        return true;
    }
    bool already_stopped = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->shutdown_requested) {
            already_stopped = true;
        }
        else {
            state->queue.push_back(WorkItem{
                .type = WorkType::kFinalize,
                .completion = std::move(completion),
            });
        }
    }
    if (already_stopped) {
        Complete(completion, PxResult<void>::Success());
        return true;
    }
    state->condition.notify_one();
    return true;
}

bool MediaRecorderSink::RequestShutdown(Completion completion) {
    const auto state = worker_state_;
    if (!state) {
        Complete(completion, PxResult<void>::Success());
        return true;
    }
    bool already_stopped = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->shutdown_requested) {
            already_stopped = true;
        }
        else {
            state->shutdown_requested = true;
            state->queue.push_back(WorkItem{
                .type = WorkType::kShutdown,
                .completion = std::move(completion),
            });
        }
    }
    if (already_stopped) {
        Complete(completion, PxResult<void>::Success());
        return true;
    }
    state->condition.notify_one();
    return true;
}

void MediaRecorderSink::JoinWorker() {
    std::lock_guard lock(lifecycle_mutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MediaRecorderSink::ShutdownForDestruction() {
    running_.store(false, std::memory_order_release);
    recording_.store(false, std::memory_order_release);
    DeactivateMediaSubscriptions();
    DeactivateClientSubscriptions();
    static_cast<void>(RequestShutdown({}));
    JoinWorker();
    keyframe_channel_->Disable();
}

void MediaRecorderSink::WorkerMain(
    const std::shared_ptr<WorkerState>& state) {
    while (true) {
        WorkItem work;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, [state] { return !state->queue.empty(); });
            work = std::move(state->queue.front());
            state->queue.pop_front();
        }

        try {
            if (work.type == WorkType::kVideo) {
                ProcessVideo(state, work.video);
            }
            else if (work.type == WorkType::kAudio) {
                ProcessAudio(state, work.audio);
            }
            else {
                FinalizeWriters(state);
            }
            Complete(work.completion, PxResult<void>::Success());
        }
        catch (const std::exception& error) {
            {
                std::lock_guard lock(state->mutex);
                ++state->writer_failures;
            }
            LOGE("event=sink.write component=media_recorder outcome=failed "
                 "reason={}", error.what());
            Complete(work.completion, PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError,
                "media_recorder.worker",
                error.what(),
                false,
                "MEDIA_RECORDER_WRITE_FAILED")));
        }
        catch (...) {
            {
                std::lock_guard lock(state->mutex);
                ++state->writer_failures;
            }
            LOGE("event=sink.write component=media_recorder outcome=failed "
                 "reason=unknown_exception");
            Complete(work.completion, PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError,
                "media_recorder.worker",
                "unknown writer exception",
                false,
                "MEDIA_RECORDER_WRITE_FAILED")));
        }
        if (work.type == WorkType::kShutdown) {
            break;
        }
    }
}

void MediaRecorderSink::ProcessVideo(
    const std::shared_ptr<WorkerState>& state,
    const std::shared_ptr<const EncodedVideoFrame>& frame) {
    if (!frame || !frame->payload || frame->payload->empty()) {
        return;
    }
    auto& writer = state->writers[frame->identity.monitor_id];
    if (!writer) {
        const std::weak_ptr<KeyframeChannel> weak_channel =
            state->keyframe_channel;
        writer = state->writer_factory(
            frame->identity.monitor_id,
            state->options,
            [weak_channel] {
                if (const auto channel = weak_channel.lock()) {
                    channel->Request();
                }
            });
        LOGI("event=record.writer component=media_recorder action=create "
             "monitor={} outcome={}",
             frame->identity.monitor_id,
             writer ? "success" : "failed");
    }
    if (writer) {
        writer->OnVideo(frame);
        std::lock_guard lock(state->mutex);
        ++state->video_packets_written;
    }
}

void MediaRecorderSink::ProcessAudio(
    const std::shared_ptr<WorkerState>& state,
    const std::shared_ptr<const EncodedAudioFrame>& frame) {
    if (!frame || !frame->payload || frame->payload->empty()) {
        return;
    }
    std::uint64_t written = 0;
    for (const auto& [monitor_id, writer] : state->writers) {
        static_cast<void>(monitor_id);
        if (writer) {
            writer->OnAudio(frame);
            ++written;
        }
    }
    std::lock_guard lock(state->mutex);
    state->audio_packets_written += written;
}

void MediaRecorderSink::FinalizeWriters(
    const std::shared_ptr<WorkerState>& state) {
    for (const auto& [monitor_id, writer] : state->writers) {
        static_cast<void>(monitor_id);
        if (writer) {
            writer->Stop();
        }
    }
    state->writers.clear();
}

MediaRecorderSink::WriterFactory MediaRecorderSink::DefaultWriterFactory() {
    return [](const std::string& monitor_id,
              const MediaRecorderOptions& options,
              const KeyframeRequester& request_keyframe) {
        RecordWriterConfig config;
        config.dir = options.record_directory;
        config.monitor_name = monitor_id;
        config.max_segment_bytes = options.max_segment_bytes;
        config.max_file_count = options.max_file_count;
        config.on_request_keyframe = request_keyframe;
        return std::make_shared<RecordWriterAdapter>(RecordWriter::Make(config));
    };
}

void MediaRecorderSink::KeyframeChannel::Request() const {
    KeyframeRequester callback;
    {
        std::lock_guard lock(mutex);
        if (!accepting) {
            return;
        }
        callback = requester;
    }
    if (callback) {
        callback();
    }
}

void MediaRecorderSink::KeyframeChannel::Disable() {
    std::lock_guard lock(mutex);
    accepting = false;
    requester = {};
}

}  // namespace px::render
