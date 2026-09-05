#include "sinks/live_pusher_sink.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "px_common_new/log.h"
#include "px_common_new/privacy_log.h"
#include "runtime/await_callback.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

RenderError MakePusherError(const RenderErrorCode code, std::string operation, std::string reason, const bool recoverable) {
    return RenderError{
        .code = code,
        .component = "live_pusher",
        .operation = std::move(operation),
        .stage = "sink",
        .reason = std::move(reason),
        .recoverable = recoverable,
    };
}

void Complete(const LivePusherSink::Completion& completion, PxResult<void> result) {
    if (!completion) {
        return;
    }
    try {
        completion(std::move(result));
    } catch (const std::exception& error) {
        static_cast<void>(error);
        LOGE("event=sink.completion component=live_pusher "
             "code=LIVE_PUSH_COMPLETION_EXCEPTION operation=invoke_completion "
             "outcome=ignored recoverable=true reason=completion_exception");
    } catch (...) {
        LOGE("event=sink.completion component=live_pusher "
             "code=LIVE_PUSH_COMPLETION_EXCEPTION operation=invoke_completion "
             "outcome=ignored recoverable=true reason=unknown_exception");
    }
}

} // namespace

std::shared_ptr<LivePusherSink> LivePusherSink::Create(std::shared_ptr<EncodedMediaBus> media_bus, LivePusherOptions options,
                                                       KeyframeRequester request_keyframe, ProcessorFactory processor_factory) {
    if (!media_bus || !processor_factory || options.queue_capacity == 0) {
        return {};
    }
    if (options.audio_bitrate <= 0) {
        options.audio_bitrate = 96000;
    }
    return std::make_shared<LivePusherSink>(std::move(media_bus), std::move(options), std::move(request_keyframe), std::move(processor_factory));
}

LivePusherSink::LivePusherSink(std::shared_ptr<EncodedMediaBus> media_bus, LivePusherOptions options, KeyframeRequester request_keyframe,
                               ProcessorFactory processor_factory)
    : media_bus_(std::move(media_bus)), options_(std::move(options)), processor_factory_(std::move(processor_factory)),
      keyframe_channel_(std::make_shared<KeyframeChannel>()), selected_monitor_(options_.primary_monitor),
      monitor_selected_(!options_.primary_monitor.empty()), enabled_(options_.enabled) {
    keyframe_channel_->requester = std::move(request_keyframe);
}

LivePusherSink::~LivePusherSink() {
    ShutdownForDestruction();
}

BuiltinModuleRegistration LivePusherSink::MakeRegistration() {
    const std::weak_ptr<LivePusherSink> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor =
            BuiltinModuleDescriptor{
                .id = std::string(kLivePusherModuleId),
                .name = "Live Pusher",
                .author = "GammaRay",
                .description = "Built-in bounded RTMP media sink",
                .version_name = "1.0.0",
                .version_code = 100,
                .capability = BuiltinModuleCapability::kSink,
                .default_enabled = options_.enabled,
            },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            if (!owner) {
                co_return std::unexpected(MakePusherError(RenderErrorCode::kModuleStartFailed, "start", "sink owner expired", false));
            }
            co_return owner->Start();
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            if (!owner) {
                co_return ModuleLifecycleResult{};
            }
            co_return co_await StopAsync(owner, std::chrono::steady_clock::now() + 5s);
        },
        .set_enabled =
            [weak_owner](const bool enabled) {
                const auto owner = weak_owner.lock();
                if (!owner) {
                    return ModuleLifecycleResult(
                        std::unexpected(MakePusherError(RenderErrorCode::kModuleLifecycleRejected, "set_enabled", "sink owner expired", false)));
                }
                return owner->SetEnabled(enabled);
            },
    };
}

ModuleLifecycleResult LivePusherSink::Start() {
    std::unique_lock lock(lifecycle_mutex_);
    if (running_.load(std::memory_order_acquire)) {
        return {};
    }
    const std::weak_ptr<KeyframeChannel> weak_channel = keyframe_channel_;
    const auto processor = processor_factory_(options_, [weak_channel] {
        if (const auto channel = weak_channel.lock()) {
            channel->Request();
        }
    });
    if (!processor) {
        return std::unexpected(MakePusherError(RenderErrorCode::kModuleStartFailed, "start", "processor factory returned no owner", false));
    }
    auto state = std::make_shared<WorkerState>();
    state->processor = processor;
    worker_state_ = state;
    worker_ = std::jthread([state] { WorkerMain(state); });
    running_.store(true, std::memory_order_release);
    const auto should_enable = enabled_.load(std::memory_order_acquire);
    lock.unlock();
    if (should_enable) {
        ActivateSubscriptions();
        keyframe_channel_->Request();
    }
    LOGI("event=sink.start component=live_pusher outcome=success enabled={} "
         "queue_capacity={} audio_bitrate={} monitor_policy={}",
         should_enable, options_.queue_capacity, options_.audio_bitrate, options_.primary_monitor.empty() ? "first_active" : "configured");
    return {};
}

PxAwaitable<ModuleLifecycleResult> LivePusherSink::StopAsync(std::shared_ptr<LivePusherSink> owner,
                                                             const std::chrono::steady_clock::time_point deadline) {
    owner->enabled_.store(false, std::memory_order_release);
    owner->running_.store(false, std::memory_order_release);
    owner->DeactivateSubscriptions();
    const std::weak_ptr<LivePusherSink> weak_owner = owner;
    const auto stopped = co_await AwaitOwnedCallback<void>(
        [weak_owner](Completion completion) {
            const auto active_owner = weak_owner.lock();
            return active_owner && active_owner->RequestControl(WorkType::kShutdown, std::move(completion));
        },
        deadline, "live_pusher.stop");
    owner->JoinWorker();
    if (!stopped) {
        co_return std::unexpected(MakePusherError(RenderErrorCode::kAsyncScopeDrainTimeout, "stop", stopped.Error().message, true));
    }
    const auto snapshot = owner->Snapshot();
    LOGI("event=sink.stop component=live_pusher outcome=success accepted={} "
         "dropped={} high_watermark={} processor_failures={}",
         snapshot.accepted_media, snapshot.dropped_media, snapshot.queue_high_watermark, snapshot.processor_failures);
    co_return ModuleLifecycleResult{};
}

ModuleLifecycleResult LivePusherSink::SetEnabled(const bool enabled) {
    if (enabled && options_.publish_url.empty()) {
        return std::unexpected(MakePusherError(RenderErrorCode::kModuleLifecycleRejected, "set_enabled", "publish URL is not configured", true));
    }
    const auto previous = enabled_.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled) {
        return {};
    }
    if (enabled) {
        ActivateSubscriptions();
        keyframe_channel_->Request();
    } else {
        DeactivateSubscriptions();
        static_cast<void>(RequestControl(WorkType::kClose, {}));
    }
    return {};
}

void LivePusherSink::ReportPerformance() {
    const auto snapshot = Snapshot();
    if (!snapshot.enabled && snapshot.dropped_media == 0) {
        return;
    }
    LOGI("event=sink.performance component=live_pusher enabled={} "
         "publishing={} queue_depth={} high_watermark={} accepted={} "
         "dropped={} processor_failures={}",
         snapshot.enabled, snapshot.publishing, snapshot.queue_depth, snapshot.queue_high_watermark, snapshot.accepted_media, snapshot.dropped_media,
         snapshot.processor_failures);
}

LivePusherSnapshot LivePusherSink::Snapshot() const {
    LivePusherSnapshot snapshot{
        .running = running_.load(std::memory_order_acquire),
        .enabled = enabled_.load(std::memory_order_acquire),
    };
    const auto state = worker_state_;
    if (!state) {
        return snapshot;
    }
    std::lock_guard lock(state->mutex);
    snapshot.publishing = state->publishing.load(std::memory_order_acquire);
    snapshot.queue_depth = state->queue.size();
    snapshot.queue_high_watermark = state->high_watermark;
    snapshot.accepted_media = state->accepted_media;
    snapshot.dropped_media = state->dropped_media;
    snapshot.processor_failures = state->processor_failures;
    return snapshot;
}

void LivePusherSink::ActivateSubscriptions() {
    std::lock_guard lock(lifecycle_mutex_);
    if (video_subscription_ || audio_subscription_ || !running_.load(std::memory_order_acquire)) {
        return;
    }
    const std::weak_ptr<LivePusherSink> weak_owner = weak_from_this();
    video_callback_ = std::make_shared<EncodedMediaBus::VideoCallback>([weak_owner](const std::shared_ptr<const EncodedVideoFrame>& frame) {
        if (const auto owner = weak_owner.lock()) {
            owner->EnqueueVideo(frame);
        }
    });
    audio_callback_ = std::make_shared<EncodedMediaBus::CapturedAudioCallback>([weak_owner](const std::shared_ptr<const CapturedAudioFrame>& frame) {
        if (const auto owner = weak_owner.lock()) {
            owner->EnqueueAudio(frame);
        }
    });
    video_subscription_ = media_bus_->SubscribeVideo(video_callback_);
    audio_subscription_ = media_bus_->SubscribeCapturedAudio(audio_callback_);
}

void LivePusherSink::DeactivateSubscriptions() {
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

void LivePusherSink::EnqueueVideo(std::shared_ptr<const EncodedVideoFrame> frame) {
    if (!frame || !frame->payload || frame->payload->empty() || (frame->codec != "h264" && frame->codec != "h265") ||
        !SelectMonitor(frame->identity.monitor_id)) {
        return;
    }
    EnqueueMedia(WorkItem{.type = WorkType::kVideo, .video = std::move(frame)});
}

void LivePusherSink::EnqueueAudio(std::shared_ptr<const CapturedAudioFrame> frame) {
    if (!frame || !frame->payload || frame->payload->empty()) {
        return;
    }
    EnqueueMedia(WorkItem{.type = WorkType::kAudio, .audio = std::move(frame)});
}

void LivePusherSink::EnqueueMedia(WorkItem work) {
    if (!running_.load(std::memory_order_acquire) || !enabled_.load(std::memory_order_acquire)) {
        return;
    }
    const auto state = worker_state_;
    if (!state) {
        return;
    }
    {
        std::lock_guard lock(state->mutex);
        if (state->shutdown_requested || !enabled_.load(std::memory_order_acquire)) {
            return;
        }
        if (state->queue.size() >= options_.queue_capacity) {
            if (work.type != WorkType::kVideo || !work.video || !work.video->key_frame) {
                ++state->dropped_media;
                return;
            }
            const auto replaceable = std::ranges::find_if(
                state->queue, [](const WorkItem& item) { return item.type == WorkType::kVideo && item.video && !item.video->key_frame; });
            if (replaceable == state->queue.end()) {
                ++state->dropped_media;
                return;
            }
            state->queue.erase(replaceable);
            ++state->dropped_media;
        }
        state->queue.push_back(std::move(work));
        ++state->accepted_media;
        state->high_watermark = std::max(state->high_watermark, state->queue.size());
    }
    state->condition.notify_one();
}

bool LivePusherSink::SelectMonitor(const std::string& monitor_id) {
    std::lock_guard lock(monitor_mutex_);
    if (!monitor_selected_) {
        selected_monitor_ = monitor_id;
        monitor_selected_ = true;
        LOGI("event=sink.monitor component=live_pusher action=select_first "
             "monitor={}",
             PrivacyLogId(monitor_id.empty() ? "game_hook" : monitor_id));
    }
    return selected_monitor_ == monitor_id;
}

bool LivePusherSink::RequestControl(const WorkType type, Completion completion) {
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
        } else {
            if (type == WorkType::kShutdown) {
                state->shutdown_requested = true;
            }
            state->queue.push_back(WorkItem{
                .type = type,
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

void LivePusherSink::JoinWorker() {
    std::lock_guard lock(lifecycle_mutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LivePusherSink::ShutdownForDestruction() {
    enabled_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    DeactivateSubscriptions();
    static_cast<void>(RequestControl(WorkType::kShutdown, {}));
    JoinWorker();
    keyframe_channel_->Disable();
}

void LivePusherSink::WorkerMain(const std::shared_ptr<WorkerState>& state) {
    while (true) {
        WorkItem work;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, [state] { return !state->queue.empty(); });
            work = std::move(state->queue.front());
            state->queue.pop_front();
        }
        try {
            if (work.type == WorkType::kVideo && state->processor) {
                state->processor->ProcessVideo(work.video);
            } else if (work.type == WorkType::kAudio && state->processor) {
                state->processor->ProcessAudio(work.audio);
            } else if (state->processor) {
                state->processor->Close();
            }
            state->publishing.store(state->processor && state->processor->IsPublishing(), std::memory_order_release);
            Complete(work.completion, PxResult<void>::Success());
        } catch (const std::exception& error) {
            {
                std::lock_guard lock(state->mutex);
                ++state->processor_failures;
            }
            LOGE("event=sink.process component=live_pusher outcome=failed "
                 "code=LIVE_PUSH_PROCESS_FAILED operation=process_work_item "
                 "recoverable=false reason=processor_exception");
            Complete(work.completion, PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "live_pusher.worker", error.what(),
                                                                               false, "LIVE_PUSH_PROCESS_FAILED")));
        } catch (...) {
            {
                std::lock_guard lock(state->mutex);
                ++state->processor_failures;
            }
            LOGE("event=sink.process component=live_pusher outcome=failed "
                 "code=LIVE_PUSH_PROCESS_FAILED operation=process_work_item "
                 "recoverable=false reason=unknown_exception");
            Complete(work.completion, PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "live_pusher.worker",
                                                                               "unknown processor exception", false, "LIVE_PUSH_PROCESS_FAILED")));
        }
        if (work.type == WorkType::kShutdown) {
            break;
        }
    }
}

void LivePusherSink::KeyframeChannel::Request() const {
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

void LivePusherSink::KeyframeChannel::Disable() {
    std::lock_guard lock(mutex);
    accepting = false;
    requester = {};
}

std::string BuildLivePublishUrl(std::string url, const std::string& live_stream_id) {
    constexpr std::string_view placeholder = "{live_stream_id}";
    if (const auto position = url.find(placeholder); position != std::string::npos) {
        url.replace(position, placeholder.size(), live_stream_id);
    }
    return url;
}

} // namespace px::render
