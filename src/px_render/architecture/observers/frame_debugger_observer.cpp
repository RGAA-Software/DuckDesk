#include "observers/frame_debugger_observer.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

#include "px_common_new/file.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

RenderError MakeFrameDebuggerError(const RenderErrorCode code,
                                   std::string operation,
                                   std::string reason,
                                   const bool recoverable) {
    return RenderError{
        .code = code,
        .component = "frame_debugger",
        .operation = std::move(operation),
        .stage = "observer",
        .reason = std::move(reason),
        .recoverable = recoverable,
    };
}

std::string SafeFilePart(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::isalnum(ch) || ch == '-' || ch == '_'
                                     ? ch
                                     : '_');
    });
    return value.empty() ? "monitor" : value;
}

std::string PayloadAsString(const ImmutableByteBuffer& payload) {
    std::string bytes;
    if (!payload) {
        return bytes;
    }
    bytes.reserve(payload->size());
    for (const auto value : *payload) {
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

}  // namespace

std::shared_ptr<FrameDebuggerObserver> FrameDebuggerObserver::Create(
    const std::shared_ptr<PxAsyncRuntime>& runtime,
    FrameDebuggerOptions options) {
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    if (!scope || options.queue_capacity == 0) {
        return {};
    }
    return std::make_shared<FrameDebuggerObserver>(scope, std::move(options));
}

FrameDebuggerObserver::FrameDebuggerObserver(
    std::shared_ptr<PxAsyncScope> worker_scope,
    FrameDebuggerOptions options)
    : worker_scope_(std::move(worker_scope)),
      options_(std::move(options)),
      raw_log_gate_(options_.raw_log_interval, 128),
      drop_log_gate_(1s, 8) {}

FrameDebuggerObserver::~FrameDebuggerObserver() {
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue;
    {
        std::lock_guard lock(queue_mutex_);
        running_.store(false, std::memory_order_release);
        queue = queue_;
    }
    if (queue) {
        queue->Close(QueueCloseMode::kCancel);
    }
    worker_scope_->BeginStop();
    CloseFiles();
}

BuiltinModuleRegistration FrameDebuggerObserver::MakeRegistration() {
    const std::weak_ptr<FrameDebuggerObserver> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kFrameDebuggerModuleId),
            .name = "Frame Debugger",
            .author = "GammaRay",
            .description = "Bounded frame diagnostics observer",
            .version_name = "2.0.0",
            .version_code = 2,
            .capability = BuiltinModuleCapability::kObserver,
            .default_enabled = false,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            if (!owner) {
                co_return std::unexpected(MakeFrameDebuggerError(
                    RenderErrorCode::kModuleStartFailed,
                    "start",
                    "observer owner expired",
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
                owner, std::chrono::steady_clock::now() + 2s);
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            if (!owner) {
                return ModuleLifecycleResult(std::unexpected(
                    MakeFrameDebuggerError(
                        RenderErrorCode::kModuleLifecycleRejected,
                        "set_enabled",
                        "observer owner expired",
                        false)));
            }
            return owner->SetEnabled(enabled);
        },
    };
}

ModuleLifecycleResult FrameDebuggerObserver::Start() {
    const auto queue = std::make_shared<BoundedMediaQueue<FrameDebuggerEvent>>(
        options_.queue_capacity, QueueOverflowPolicy::kDropOldest);
    const auto completion = PxAsyncOneShot<void>::Create(
        worker_scope_->Executor());
    const std::weak_ptr<FrameDebuggerObserver> weak_owner = weak_from_this();
    const auto executor = worker_scope_->Executor();
    bool accepted = false;
    {
        std::lock_guard lock(queue_mutex_);
        if (running_.load(std::memory_order_acquire)) {
            return {};
        }
        queue_ = queue;
        consumer_completion_ = completion;
        accepted = worker_scope_->Spawn(
            "frame_debugger_consumer",
            [weak_owner, queue, completion, executor]() mutable {
                return ConsumeLoop(weak_owner,
                                   std::move(queue),
                                   std::move(completion),
                                   std::move(executor));
            });
        if (accepted) {
            running_.store(true, std::memory_order_release);
        }
    }
    if (!accepted) {
        queue->Close(QueueCloseMode::kCancel);
        return std::unexpected(MakeFrameDebuggerError(
            RenderErrorCode::kModuleStartFailed,
            "start",
            "worker scope rejected the consumer",
            true));
    }
    LOGI("event=observer.start component=frame_debugger queue_capacity={} "
         "save_encoded_video={} outcome=success",
         options_.queue_capacity,
         options_.save_encoded_video);
    return {};
}

PxAwaitable<ModuleLifecycleResult> FrameDebuggerObserver::StopAsync(
    const std::shared_ptr<FrameDebuggerObserver>& owner,
    const std::chrono::steady_clock::time_point deadline) {
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue;
    std::shared_ptr<PxAsyncOneShot<void>> completion;
    {
        std::lock_guard lock(owner->queue_mutex_);
        if (!owner->running_.exchange(false, std::memory_order_acq_rel)) {
            co_return ModuleLifecycleResult{};
        }
        queue = owner->queue_;
        completion = owner->consumer_completion_;
    }
    if (!queue || !completion) {
        co_return std::unexpected(MakeFrameDebuggerError(
            RenderErrorCode::kModuleStopFailed,
            "stop",
            "consumer state is incomplete",
            false));
    }
    queue->Close(QueueCloseMode::kDrain);
    const auto drained = co_await PxAsyncOneShot<void>::WaitUntil(
        completion, deadline);
    if (!drained) {
        queue->Close(QueueCloseMode::kCancel);
        co_return std::unexpected(MakeFrameDebuggerError(
            RenderErrorCode::kAsyncScopeDrainTimeout,
            "stop",
            "encoded frame queue did not drain before deadline",
            true));
    }
    owner->CloseFiles();
    const auto snapshot = queue->Snapshot();
    LOGI("event=observer.stop component=frame_debugger outcome=success "
         "accepted={} dropped={} high_watermark={} bytes_written={} "
         "write_failures={}",
         snapshot.accepted,
         snapshot.dropped,
         snapshot.high_watermark,
         owner->encoded_bytes_written_.load(std::memory_order_relaxed),
         owner->file_write_failures_.load(std::memory_order_relaxed));
    co_return ModuleLifecycleResult{};
}

ModuleLifecycleResult FrameDebuggerObserver::SetEnabled(const bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
    return {};
}

FrameDebuggerSubmitResult FrameDebuggerObserver::SubmitEncoderReady(
    VideoEncoderReady event) {
    return SubmitEvent(std::make_shared<const FrameDebuggerEvent>(
        std::move(event)));
}

FrameDebuggerSubmitResult FrameDebuggerObserver::SubmitEncodedFrame(
    std::shared_ptr<const EncodedVideoFrame> frame) {
    if (!frame || !frame->payload || frame->payload->empty()) {
        return FrameDebuggerSubmitResult::kStopped;
    }
    encoded_frames_submitted_.fetch_add(1, std::memory_order_relaxed);
    return SubmitEvent(std::make_shared<const FrameDebuggerEvent>(
        std::move(frame)));
}

FrameDebuggerSubmitResult FrameDebuggerObserver::SubmitClientConnected() {
    return SubmitEvent(std::make_shared<const FrameDebuggerEvent>(
        FrameDebuggerClientConnected{}));
}

void FrameDebuggerObserver::ObserveRawFrame(RawVideoFrameObservation frame) {
    if (!running_.load(std::memory_order_acquire) ||
        !enabled_.load(std::memory_order_acquire) || frame.monitor_id.empty()) {
        return;
    }
    raw_frames_observed_.fetch_add(1, std::memory_order_relaxed);
    const auto decision = raw_log_gate_.Evaluate(
        "raw_frame:" + frame.monitor_id, std::chrono::steady_clock::now());
    if (decision.emit) {
        LOGI("event=observer.raw_frame component=frame_debugger monitor={} "
             "frame_index={} width={} height={} suppressed={} outcome=observed",
             frame.monitor_id,
             frame.frame_index,
             frame.width,
             frame.height,
             decision.suppressed_since_last_emit);
    }
}

bool FrameDebuggerObserver::WantsEncodedFrames() const noexcept {
    return options_.save_encoded_video &&
           running_.load(std::memory_order_acquire) &&
           enabled_.load(std::memory_order_acquire);
}

FrameDebuggerSnapshot FrameDebuggerObserver::Snapshot() const {
    MediaQueueSnapshot queue_snapshot;
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue;
    {
        std::lock_guard lock(queue_mutex_);
        queue = queue_;
    }
    if (queue) {
        queue_snapshot = queue->Snapshot();
    }
    return FrameDebuggerSnapshot{
        .running = running_.load(std::memory_order_acquire),
        .enabled = enabled_.load(std::memory_order_acquire),
        .raw_frames_observed =
            raw_frames_observed_.load(std::memory_order_relaxed),
        .encoded_frames_submitted =
            encoded_frames_submitted_.load(std::memory_order_relaxed),
        .encoded_bytes_written =
            encoded_bytes_written_.load(std::memory_order_relaxed),
        .file_write_failures =
            file_write_failures_.load(std::memory_order_relaxed),
        .queue = queue_snapshot,
    };
}

PxAwaitable<void> FrameDebuggerObserver::ConsumeLoop(
    std::weak_ptr<FrameDebuggerObserver> weak_owner,
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue,
    std::shared_ptr<PxAsyncOneShot<void>> completion,
    asio::any_io_executor executor) {
    for (;;) {
        const auto item = queue->TryPop();
        if (item) {
            if (const auto owner = weak_owner.lock()) {
                owner->ProcessEvent(**item);
            }
            continue;
        }
        const auto snapshot = queue->Snapshot();
        if (snapshot.closed || weak_owner.expired()) {
            static_cast<void>(completion->TryComplete(PxResult<void>::Success()));
            co_return;
        }
        const auto timer = std::make_shared<asio::steady_timer>(executor);
        timer->expires_after(5ms);
        asio::error_code wait_error;
        co_await timer->async_wait(
            asio::redirect_error(asio::use_awaitable, wait_error));
        const auto cancellation = co_await asio::this_coro::cancellation_state;
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            queue->Close(QueueCloseMode::kCancel);
        }
    }
}

void FrameDebuggerObserver::ProcessEvent(const FrameDebuggerEvent& event) {
    switch (event.index()) {
        case 0:
            ProcessEncoderReady(std::get<VideoEncoderReady>(event));
            return;
        case 1:
            ProcessEncodedFrame(
                std::get<std::shared_ptr<const EncodedVideoFrame>>(event));
            return;
        case 2:
            rotate_on_next_encoder_ = true;
            return;
        default:
            return;
    }
}

void FrameDebuggerObserver::ProcessEncoderReady(
    const VideoEncoderReady& event) {
    if (!options_.save_encoded_video || event.monitor_id.empty()) {
        return;
    }
    if (rotate_on_next_encoder_) {
        CloseFiles();
        rotate_on_next_encoder_ = false;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(options_.output_directory,
                                        directory_error);
    if (directory_error) {
        file_write_failures_.fetch_add(1, std::memory_order_relaxed);
        LOGE("event=observer.file_open component=frame_debugger "
             "code={} monitor={} "
             "native_code={} outcome=disabled reason={}",
             StableErrorCode(
                 RenderErrorCode::kFrameDebuggerDirectoryFailed),
             event.monitor_id,
             directory_error.value(),
             directory_error.message());
        return;
    }
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now()
                                   .time_since_epoch())
                               .count();
    const auto extension = event.codec == "h264" ? "h264" : "h265";
    const auto filename = std::format("enc_{}_{}.{}",
                                      SafeFilePart(event.monitor_id),
                                      timestamp,
                                      extension);
    const auto path = options_.output_directory / filename;
    const auto file = File::OpenForAppendB(U8Path(path));
    if (!file || !file->IsOpen()) {
        file_write_failures_.fetch_add(1, std::memory_order_relaxed);
        LOGE("event=observer.file_open component=frame_debugger "
             "code={} monitor={} codec={} "
             "outcome=disabled",
             StableErrorCode(
                 RenderErrorCode::kFrameDebuggerFileOpenFailed),
             event.monitor_id,
             event.codec);
        return;
    }
    if (const auto existing = encoded_video_files_.find(event.monitor_id);
        existing != encoded_video_files_.end()) {
        existing->second->Close();
    }
    encoded_video_files_[event.monitor_id] = file;
}

void FrameDebuggerObserver::ProcessEncodedFrame(
    const std::shared_ptr<const EncodedVideoFrame>& frame) {
    if (!options_.save_encoded_video || !frame || !frame->payload) {
        return;
    }
    const auto file = encoded_video_files_.find(frame->identity.monitor_id);
    if (file == encoded_video_files_.end() || !file->second) {
        return;
    }
    const auto bytes = PayloadAsString(frame->payload);
    const auto written = file->second->Append(bytes);
    if (written != static_cast<std::int64_t>(bytes.size())) {
        file_write_failures_.fetch_add(1, std::memory_order_relaxed);
        LOGE("event=observer.file_write component=frame_debugger "
             "code={} monitor={} "
             "frame_index={} expected_bytes={} actual_bytes={} outcome=dropped",
             StableErrorCode(
                 RenderErrorCode::kFrameDebuggerFileWriteFailed),
             frame->identity.monitor_id,
             frame->identity.frame_index,
             bytes.size(),
             written);
        return;
    }
    encoded_bytes_written_.fetch_add(
        static_cast<std::uint64_t>(written), std::memory_order_relaxed);
}

void FrameDebuggerObserver::CloseFiles() {
    for (const auto& [monitor_id, file] : encoded_video_files_) {
        static_cast<void>(monitor_id);
        if (file) {
            file->Close();
        }
    }
    encoded_video_files_.clear();
}

FrameDebuggerSubmitResult FrameDebuggerObserver::SubmitEvent(
    std::shared_ptr<const FrameDebuggerEvent> event) {
    if (!enabled_.load(std::memory_order_acquire)) {
        return FrameDebuggerSubmitResult::kDisabled;
    }
    if (!running_.load(std::memory_order_acquire)) {
        return FrameDebuggerSubmitResult::kStopped;
    }
    std::shared_ptr<BoundedMediaQueue<FrameDebuggerEvent>> queue;
    {
        std::lock_guard lock(queue_mutex_);
        queue = queue_;
    }
    if (!running_.load(std::memory_order_acquire) || !queue) {
        return FrameDebuggerSubmitResult::kStopped;
    }
    const auto result = queue->Submit(std::move(event));
    switch (result) {
        case QueueSubmitResult::kAccepted:
            return FrameDebuggerSubmitResult::kAccepted;
        case QueueSubmitResult::kAcceptedAfterDroppingOldest:
            if (const auto decision = drop_log_gate_.Evaluate(
                    "encoded_frame", std::chrono::steady_clock::now());
                decision.emit) {
                const auto snapshot = queue->Snapshot();
                LOGW("event=observer.queue_overflow component=frame_debugger "
                     "code={} policy=drop_oldest depth={} dropped={} "
                     "high_watermark={} suppressed={} outcome=continued",
                     StableErrorCode(RenderErrorCode::kObserverQueueOverflow),
                     snapshot.depth,
                     snapshot.dropped,
                     snapshot.high_watermark,
                     decision.suppressed_since_last_emit);
            }
            return FrameDebuggerSubmitResult::kAcceptedAfterDroppingOldest;
        case QueueSubmitResult::kDroppedNewest:
            return FrameDebuggerSubmitResult::kAcceptedAfterDroppingOldest;
        case QueueSubmitResult::kRejectedClosed:
            return FrameDebuggerSubmitResult::kStopped;
    }
    return FrameDebuggerSubmitResult::kStopped;
}

}  // namespace px::render
