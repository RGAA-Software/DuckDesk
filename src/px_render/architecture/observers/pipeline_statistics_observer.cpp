#include "observers/pipeline_statistics_observer.h"

#include <utility>
#include <vector>

#include "px_common/log.h"

namespace px::render {
namespace {

RenderError MakeStatisticsError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "pipeline_statistics",
        .operation = std::move(operation),
        .stage = "observer",
        .reason = std::move(reason),
        .recoverable = false,
    };
}

std::string ClientKey(const std::string& visitor_device_id,
                      const std::string& stream_id) {
    return visitor_device_id.empty() ? stream_id : visitor_device_id;
}

}  // namespace

std::shared_ptr<PipelineStatisticsObserver>
PipelineStatisticsObserver::Create(std::shared_ptr<EncodedMediaBus> media_bus) {
    if (!media_bus) {
        return {};
    }
    return std::make_shared<PipelineStatisticsObserver>(std::move(media_bus));
}

PipelineStatisticsObserver::PipelineStatisticsObserver(
    std::shared_ptr<EncodedMediaBus> media_bus)
    : media_bus_(std::move(media_bus)) {}

PipelineStatisticsObserver::~PipelineStatisticsObserver() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration PipelineStatisticsObserver::MakeRegistration() {
    const std::weak_ptr<PipelineStatisticsObserver> weak_owner =
        weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kPipelineStatisticsModuleId),
            .name = "Pipeline Statistics",
            .author = "GammaRay",
            .description = "Typed media and session telemetry observer",
            .version_name = "1.0.0",
            .version_code = 100,
            .capability = BuiltinModuleCapability::kObserver,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeStatisticsError(
                      "start", "observer owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeStatisticsError(
                      "set_enabled", "observer owner expired")));
        },
    };
}

ModuleLifecycleResult PipelineStatisticsObserver::Start() {
    std::lock_guard lock(lifecycle_mutex_);
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return {};
    }
    const std::weak_ptr<PipelineStatisticsObserver> weak_owner =
        weak_from_this();
    video_callback_ = std::make_shared<EncodedMediaBus::VideoCallback>(
        [weak_owner](const std::shared_ptr<const EncodedVideoFrame>& frame) {
            if (const auto owner = weak_owner.lock()) {
                owner->OnVideo(frame);
            }
        });
    captured_video_callback_ =
        std::make_shared<EncodedMediaBus::CapturedVideoCallback>(
            [weak_owner](
                const std::shared_ptr<const CapturedVideoFrame>& frame) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnCapturedVideo(frame);
                }
            });
    encoded_audio_callback_ =
        std::make_shared<EncodedMediaBus::EncodedAudioCallback>(
            [weak_owner](
                const std::shared_ptr<const EncodedAudioFrame>& frame) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnEncodedAudio(frame);
                }
            });
    captured_audio_callback_ =
        std::make_shared<EncodedMediaBus::CapturedAudioCallback>(
            [weak_owner](
                const std::shared_ptr<const CapturedAudioFrame>& frame) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnCapturedAudio(frame);
                }
            });
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
    video_subscription_ = media_bus_->SubscribeVideo(video_callback_);
    captured_video_subscription_ =
        media_bus_->SubscribeCapturedVideo(captured_video_callback_);
    encoded_audio_subscription_ =
        media_bus_->SubscribeEncodedAudio(encoded_audio_callback_);
    captured_audio_subscription_ =
        media_bus_->SubscribeCapturedAudio(captured_audio_callback_);
    client_connected_subscription_ =
        media_bus_->SubscribeClientConnected(client_connected_callback_);
    client_disconnected_subscription_ =
        media_bus_->SubscribeClientDisconnected(client_disconnected_callback_);
    last_report_ = std::chrono::steady_clock::now();
    last_report_snapshot_ = Snapshot();
    LOGI("event=observer.start component=pipeline_statistics outcome=success");
    return {};
}

ModuleLifecycleResult PipelineStatisticsObserver::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return {};
    }
    ResetSubscriptions();
    const auto snapshot = Snapshot();
    LOGI("event=observer.stop component=pipeline_statistics outcome=success "
         "video_frames={} video_bytes={} captured_video_frames={} "
         "captured_video_bytes={} encoded_audio_packets={} "
         "encoded_audio_bytes={} raw_audio_frames={} raw_audio_bytes={}",
         snapshot.encoded_video_frames, snapshot.encoded_video_bytes,
         snapshot.captured_video_frames, snapshot.captured_video_bytes,
         snapshot.encoded_audio_packets, snapshot.encoded_audio_bytes,
         snapshot.captured_audio_frames, snapshot.captured_audio_bytes);
    return {};
}

ModuleLifecycleResult PipelineStatisticsObserver::SetEnabled(
    const bool enabled) {
    enabled_.store(enabled, std::memory_order_release);
    return {};
}

PipelineStatisticsSnapshot PipelineStatisticsObserver::Snapshot() const {
    std::size_t clients = 0;
    {
        std::lock_guard lock(clients_mutex_);
        clients = connected_clients_.size();
    }
    return PipelineStatisticsSnapshot{
        .running = running_.load(std::memory_order_acquire),
        .encoded_video_frames = video_frames_.load(std::memory_order_relaxed),
        .encoded_video_bytes = video_bytes_.load(std::memory_order_relaxed),
        .captured_video_frames =
            captured_video_frames_.load(std::memory_order_relaxed),
        .captured_video_bytes =
            captured_video_bytes_.load(std::memory_order_relaxed),
        .encoded_audio_packets =
            encoded_audio_packets_.load(std::memory_order_relaxed),
        .encoded_audio_bytes =
            encoded_audio_bytes_.load(std::memory_order_relaxed),
        .captured_audio_frames =
            captured_audio_frames_.load(std::memory_order_relaxed),
        .captured_audio_bytes =
            captured_audio_bytes_.load(std::memory_order_relaxed),
        .connected_clients = clients,
    };
}

void PipelineStatisticsObserver::ReportPerformance() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(report_mutex_);
    const auto elapsed = now - last_report_;
    if (elapsed < std::chrono::seconds(5)) {
        return;
    }
    const auto current = Snapshot();
    const auto seconds = std::chrono::duration<double>(elapsed).count();
    const auto video_frames = current.encoded_video_frames -
                              last_report_snapshot_.encoded_video_frames;
    const auto video_bytes = current.encoded_video_bytes -
                             last_report_snapshot_.encoded_video_bytes;
    const auto captured_video_frames = current.captured_video_frames -
                                       last_report_snapshot_.captured_video_frames;
    const auto captured_video_bytes = current.captured_video_bytes -
                                      last_report_snapshot_.captured_video_bytes;
    const auto audio_packets = current.encoded_audio_packets -
                               last_report_snapshot_.encoded_audio_packets;
    LOGI("event=pipeline.performance component=pipeline_statistics "
         "video_fps={:.2f} video_mbps={:.3f} encoded_audio_pps={:.2f} "
         "captured_video_fps={:.2f} captured_video_mbps={:.3f} "
         "clients={} outcome=sampled",
         static_cast<double>(video_frames) / seconds,
         static_cast<double>(video_bytes) * 8.0 / seconds / 1000000.0,
         static_cast<double>(audio_packets) / seconds,
         static_cast<double>(captured_video_frames) / seconds,
         static_cast<double>(captured_video_bytes) * 8.0 / seconds / 1000000.0,
         current.connected_clients);
    last_report_ = now;
    last_report_snapshot_ = current;
}

void PipelineStatisticsObserver::ResetSubscriptions() {
    std::vector<std::shared_ptr<ScopedSubscription>> subscriptions;
    {
        std::lock_guard lock(lifecycle_mutex_);
        subscriptions = {
            std::move(video_subscription_),
            std::move(captured_video_subscription_),
            std::move(encoded_audio_subscription_),
            std::move(captured_audio_subscription_),
            std::move(client_connected_subscription_),
            std::move(client_disconnected_subscription_),
        };
        video_callback_.reset();
        captured_video_callback_.reset();
        encoded_audio_callback_.reset();
        captured_audio_callback_.reset();
        client_connected_callback_.reset();
        client_disconnected_callback_.reset();
    }
    for (const auto& subscription : subscriptions) {
        if (subscription) {
            subscription->Reset();
        }
    }
}

void PipelineStatisticsObserver::OnCapturedVideo(
    const std::shared_ptr<const CapturedVideoFrame>& frame) {
    if (!enabled_.load(std::memory_order_acquire) || !frame ||
        !frame->Payload()) {
        return;
    }
    captured_video_frames_.fetch_add(1, std::memory_order_relaxed);
    captured_video_bytes_.fetch_add(
        frame->Payload()->size(), std::memory_order_relaxed);
}

void PipelineStatisticsObserver::OnVideo(
    const std::shared_ptr<const EncodedVideoFrame>& frame) {
    if (!enabled_.load(std::memory_order_acquire) || !frame ||
        !frame->payload) {
        return;
    }
    video_frames_.fetch_add(1, std::memory_order_relaxed);
    video_bytes_.fetch_add(frame->payload->size(), std::memory_order_relaxed);
}

void PipelineStatisticsObserver::OnEncodedAudio(
    const std::shared_ptr<const EncodedAudioFrame>& frame) {
    if (!enabled_.load(std::memory_order_acquire) || !frame ||
        !frame->payload) {
        return;
    }
    encoded_audio_packets_.fetch_add(1, std::memory_order_relaxed);
    encoded_audio_bytes_.fetch_add(
        frame->payload->size(), std::memory_order_relaxed);
}

void PipelineStatisticsObserver::OnCapturedAudio(
    const std::shared_ptr<const CapturedAudioFrame>& frame) {
    if (!enabled_.load(std::memory_order_acquire) || !frame ||
        !frame->payload) {
        return;
    }
    captured_audio_frames_.fetch_add(1, std::memory_order_relaxed);
    captured_audio_bytes_.fetch_add(
        frame->payload->size(), std::memory_order_relaxed);
}

void PipelineStatisticsObserver::OnClientConnected(
    const MediaClientConnected& event) {
    if (!enabled_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard lock(clients_mutex_);
    connected_clients_.insert(ClientKey(
        event.visitor_device_id, event.stream_id));
}

void PipelineStatisticsObserver::OnClientDisconnected(
    const MediaClientDisconnected& event) {
    std::lock_guard lock(clients_mutex_);
    connected_clients_.erase(ClientKey(
        event.visitor_device_id, event.stream_id));
}

}  // namespace px::render
