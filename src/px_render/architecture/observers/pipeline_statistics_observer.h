#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "modules/builtin_module_catalog.h"
#include "pipeline/encoded_media_bus.h"

namespace px::render {

inline constexpr std::string_view kPipelineStatisticsModuleId =
    "58139f8b-c0dc-4abe-a266-a199adb2ddea";

struct PipelineStatisticsSnapshot final {
    bool running{false};
    std::uint64_t encoded_video_frames{0};
    std::uint64_t encoded_video_bytes{0};
    std::uint64_t captured_video_frames{0};
    std::uint64_t captured_video_bytes{0};
    std::uint64_t encoded_audio_packets{0};
    std::uint64_t encoded_audio_bytes{0};
    std::uint64_t captured_audio_frames{0};
    std::uint64_t captured_audio_bytes{0};
    std::size_t connected_clients{0};
};

// Atomic observer for critical media-volume and session-rate telemetry.
// Callbacks do constant-time accounting only and never enqueue or block.
class PipelineStatisticsObserver final
    : public std::enable_shared_from_this<PipelineStatisticsObserver> {
public:
    [[nodiscard]] static std::shared_ptr<PipelineStatisticsObserver> Create(
        std::shared_ptr<EncodedMediaBus> media_bus);

    explicit PipelineStatisticsObserver(
        std::shared_ptr<EncodedMediaBus> media_bus);
    ~PipelineStatisticsObserver();

    PipelineStatisticsObserver(const PipelineStatisticsObserver&) = delete;
    PipelineStatisticsObserver& operator=(
        const PipelineStatisticsObserver&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);
    [[nodiscard]] PipelineStatisticsSnapshot Snapshot() const;
    void ReportPerformance();

private:
    void ResetSubscriptions();
    void OnVideo(const std::shared_ptr<const EncodedVideoFrame>& frame);
    void OnCapturedVideo(
        const std::shared_ptr<const CapturedVideoFrame>& frame);
    void OnEncodedAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame);
    void OnCapturedAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame);
    void OnClientConnected(const MediaClientConnected& event);
    void OnClientDisconnected(const MediaClientDisconnected& event);

    const std::shared_ptr<EncodedMediaBus> media_bus_;
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex clients_mutex_;
    std::set<std::string> connected_clients_;

    std::shared_ptr<EncodedMediaBus::VideoCallback> video_callback_;
    std::shared_ptr<EncodedMediaBus::CapturedVideoCallback>
        captured_video_callback_;
    std::shared_ptr<EncodedMediaBus::EncodedAudioCallback>
        encoded_audio_callback_;
    std::shared_ptr<EncodedMediaBus::CapturedAudioCallback>
        captured_audio_callback_;
    std::shared_ptr<EncodedMediaBus::ClientConnectedCallback>
        client_connected_callback_;
    std::shared_ptr<EncodedMediaBus::ClientDisconnectedCallback>
        client_disconnected_callback_;
    std::shared_ptr<ScopedSubscription> video_subscription_;
    std::shared_ptr<ScopedSubscription> captured_video_subscription_;
    std::shared_ptr<ScopedSubscription> encoded_audio_subscription_;
    std::shared_ptr<ScopedSubscription> captured_audio_subscription_;
    std::shared_ptr<ScopedSubscription> client_connected_subscription_;
    std::shared_ptr<ScopedSubscription> client_disconnected_subscription_;

    std::atomic_bool running_{false};
    std::atomic_bool enabled_{true};
    std::atomic_uint64_t video_frames_{0};
    std::atomic_uint64_t video_bytes_{0};
    std::atomic_uint64_t captured_video_frames_{0};
    std::atomic_uint64_t captured_video_bytes_{0};
    std::atomic_uint64_t encoded_audio_packets_{0};
    std::atomic_uint64_t encoded_audio_bytes_{0};
    std::atomic_uint64_t captured_audio_frames_{0};
    std::atomic_uint64_t captured_audio_bytes_{0};

    mutable std::mutex report_mutex_;
    std::chrono::steady_clock::time_point last_report_{};
    PipelineStatisticsSnapshot last_report_snapshot_;
};

}  // namespace px::render
