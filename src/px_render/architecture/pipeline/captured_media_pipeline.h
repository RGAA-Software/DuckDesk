#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../diagnostics/render_error.h"
#include "media_types.h"
#include "../runtime/scoped_subscription.h"

namespace px::render {

using MediaSubmitResult = std::expected<void, RenderError>;
using CapturedVideoProcessResult = std::expected<
    std::shared_ptr<const CapturedVideoFrame>, RenderError>;
using CapturedAudioProcessResult = std::expected<
    std::shared_ptr<const CapturedAudioFrame>, RenderError>;

class CapturedMediaPipeline;

// A source plug-in receives this narrow publishing capability. The port does
// not own the pipeline, so a late source callback becomes a typed rejection.
class MediaSourcePort final {
public:
    explicit MediaSourcePort(std::weak_ptr<CapturedMediaPipeline> pipeline);
    [[nodiscard]] MediaSubmitResult PublishVideo(
        const std::shared_ptr<const CapturedVideoFrame>& frame) const;
    [[nodiscard]] MediaSubmitResult PublishAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame) const;

private:
    std::weak_ptr<CapturedMediaPipeline> pipeline_;
};

struct CapturedMediaPipelineSnapshot final {
    std::size_t video_processors{0};
    std::size_t audio_processors{0};
    std::uint64_t video_received{0};
    std::uint64_t video_delivered{0};
    std::uint64_t video_dropped{0};
    std::uint64_t video_failed{0};
    std::uint64_t audio_received{0};
    std::uint64_t audio_delivered{0};
    std::uint64_t audio_dropped{0};
    std::uint64_t audio_failed{0};
};

// Lifetime:
// - Owned by RdApplication/composition.
// - Source ports and subscriptions retain only weak ownership.
// - Processor entries retain callbacks through weak_ptr; the plug-in owns its
//   callback and ScopedSubscription.
//
// Threading:
// - Registration state is protected by mutex_.
// - Processor and output callbacks execute without mutex_ held.
// - Per-frame processing is synchronous and never suspends or queues work.
class CapturedMediaPipeline final
    : public std::enable_shared_from_this<CapturedMediaPipeline> {
public:
    using VideoProcessor = std::function<CapturedVideoProcessResult(
        const std::shared_ptr<const CapturedVideoFrame>&)>;
    using AudioProcessor = std::function<CapturedAudioProcessResult(
        const std::shared_ptr<const CapturedAudioFrame>&)>;
    using VideoOutput = std::function<MediaSubmitResult(
        const std::shared_ptr<const CapturedVideoFrame>&)>;
    using AudioOutput = std::function<MediaSubmitResult(
        const std::shared_ptr<const CapturedAudioFrame>&)>;

    [[nodiscard]] static std::shared_ptr<CapturedMediaPipeline> Create(
        VideoOutput video_output,
        AudioOutput audio_output);

    CapturedMediaPipeline(VideoOutput video_output, AudioOutput audio_output);
    CapturedMediaPipeline(const CapturedMediaPipeline&) = delete;
    CapturedMediaPipeline& operator=(const CapturedMediaPipeline&) = delete;

    [[nodiscard]] std::shared_ptr<MediaSourcePort> CreateSourcePort();
    [[nodiscard]] std::expected<std::shared_ptr<ScopedSubscription>, RenderError>
    RegisterVideoProcessor(
        std::string id,
        int order,
        const std::shared_ptr<VideoProcessor>& processor);
    [[nodiscard]] std::expected<std::shared_ptr<ScopedSubscription>, RenderError>
    RegisterAudioProcessor(
        std::string id,
        int order,
        const std::shared_ptr<AudioProcessor>& processor);

    [[nodiscard]] MediaSubmitResult SubmitVideo(
        const std::shared_ptr<const CapturedVideoFrame>& frame);
    [[nodiscard]] MediaSubmitResult SubmitAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame);
    [[nodiscard]] bool HasVideoProcessors() const;
    [[nodiscard]] bool HasAudioProcessors() const;
    [[nodiscard]] CapturedMediaPipelineSnapshot Snapshot() const;

private:
    template <typename Callback>
    struct ProcessorEntry final {
        std::uint64_t registration_id{0};
        std::string id;
        int order{0};
        std::weak_ptr<Callback> callback;
        std::atomic_bool active{true};
    };

    using VideoEntry = ProcessorEntry<VideoProcessor>;
    using AudioEntry = ProcessorEntry<AudioProcessor>;

    void UnregisterVideo(std::uint64_t registration_id);
    void UnregisterAudio(std::uint64_t registration_id);
    [[nodiscard]] std::vector<std::shared_ptr<VideoEntry>> VideoSnapshot();
    [[nodiscard]] std::vector<std::shared_ptr<AudioEntry>> AudioSnapshot();

    const VideoOutput video_output_;
    const AudioOutput audio_output_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<VideoEntry>> video_processors_;
    std::vector<std::shared_ptr<AudioEntry>> audio_processors_;
    std::uint64_t next_registration_id_{1};

    std::atomic_uint64_t video_received_{0};
    std::atomic_uint64_t video_delivered_{0};
    std::atomic_uint64_t video_dropped_{0};
    std::atomic_uint64_t video_failed_{0};
    std::atomic_uint64_t audio_received_{0};
    std::atomic_uint64_t audio_delivered_{0};
    std::atomic_uint64_t audio_dropped_{0};
    std::atomic_uint64_t audio_failed_{0};
};

}  // namespace px::render
