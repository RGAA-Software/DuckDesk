#include "pipeline/captured_media_pipeline.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace px::render {
namespace {

RenderError PipelineError(const RenderErrorCode code,
                          std::string operation,
                          std::string reason) {
    return RenderError{
        .code = code,
        .component = "captured_media_pipeline",
        .operation = std::move(operation),
        .stage = "processor_chain",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

RenderError ExpiredPipelineError(const std::string& operation) {
    return PipelineError(RenderErrorCode::kModuleDependencyUnavailable,
                         operation,
                         "captured media pipeline is unavailable");
}

}  // namespace

MediaSourcePort::MediaSourcePort(
    std::weak_ptr<CapturedMediaPipeline> pipeline)
    : pipeline_(std::move(pipeline)) {}

MediaSubmitResult MediaSourcePort::PublishVideo(
    const std::shared_ptr<const CapturedVideoFrame>& frame) const {
    const auto pipeline = pipeline_.lock();
    return pipeline ? pipeline->SubmitVideo(frame)
                    : MediaSubmitResult(std::unexpected(
                          ExpiredPipelineError("publish_video")));
}

MediaSubmitResult MediaSourcePort::PublishAudio(
    const std::shared_ptr<const CapturedAudioFrame>& frame) const {
    const auto pipeline = pipeline_.lock();
    return pipeline ? pipeline->SubmitAudio(frame)
                    : MediaSubmitResult(std::unexpected(
                          ExpiredPipelineError("publish_audio")));
}

std::shared_ptr<CapturedMediaPipeline> CapturedMediaPipeline::Create(
    VideoOutput video_output,
    AudioOutput audio_output) {
    if (!video_output || !audio_output) {
        return {};
    }
    return std::make_shared<CapturedMediaPipeline>(
        std::move(video_output), std::move(audio_output));
}

CapturedMediaPipeline::CapturedMediaPipeline(
    VideoOutput video_output,
    AudioOutput audio_output)
    : video_output_(std::move(video_output)),
      audio_output_(std::move(audio_output)) {}

std::shared_ptr<MediaSourcePort>
CapturedMediaPipeline::CreateSourcePort() {
    return std::make_shared<MediaSourcePort>(weak_from_this());
}

std::expected<std::shared_ptr<ScopedSubscription>, RenderError>
CapturedMediaPipeline::RegisterVideoProcessor(
    std::string id,
    const int order,
    const std::shared_ptr<VideoProcessor>& processor) {
    if (id.empty() || !processor) {
        return std::unexpected(PipelineError(
            RenderErrorCode::kModuleInvalidDescriptor,
            "register_video_processor",
            "processor id and owned callback are required"));
    }
    const auto entry = std::make_shared<VideoEntry>();
    {
        std::lock_guard lock(mutex_);
        const auto duplicate = std::ranges::any_of(
            video_processors_, [&id](const auto& current) {
                return current->active.load(std::memory_order_acquire) &&
                       current->id == id;
            });
        if (duplicate) {
            return std::unexpected(PipelineError(
                RenderErrorCode::kModuleAlreadyRegistered,
                "register_video_processor",
                "video processor is already registered: " + id));
        }
        entry->registration_id = next_registration_id_++;
        entry->id = std::move(id);
        entry->order = order;
        entry->callback = processor;
        video_processors_.push_back(entry);
        std::ranges::sort(video_processors_, {}, [](const auto& current) {
            return std::pair(current->order, current->registration_id);
        });
    }
    const auto weak_self = weak_from_this();
    const std::weak_ptr<VideoEntry> weak_entry = entry;
    return std::make_shared<ScopedSubscription>(
        [weak_self, weak_entry, id = entry->registration_id] {
            if (const auto active_entry = weak_entry.lock()) {
                active_entry->active.store(false, std::memory_order_release);
            }
            if (const auto self = weak_self.lock()) {
                self->UnregisterVideo(id);
            }
        });
}

std::expected<std::shared_ptr<ScopedSubscription>, RenderError>
CapturedMediaPipeline::RegisterAudioProcessor(
    std::string id,
    const int order,
    const std::shared_ptr<AudioProcessor>& processor) {
    if (id.empty() || !processor) {
        return std::unexpected(PipelineError(
            RenderErrorCode::kModuleInvalidDescriptor,
            "register_audio_processor",
            "processor id and owned callback are required"));
    }
    const auto entry = std::make_shared<AudioEntry>();
    {
        std::lock_guard lock(mutex_);
        const auto duplicate = std::ranges::any_of(
            audio_processors_, [&id](const auto& current) {
                return current->active.load(std::memory_order_acquire) &&
                       current->id == id;
            });
        if (duplicate) {
            return std::unexpected(PipelineError(
                RenderErrorCode::kModuleAlreadyRegistered,
                "register_audio_processor",
                "audio processor is already registered: " + id));
        }
        entry->registration_id = next_registration_id_++;
        entry->id = std::move(id);
        entry->order = order;
        entry->callback = processor;
        audio_processors_.push_back(entry);
        std::ranges::sort(audio_processors_, {}, [](const auto& current) {
            return std::pair(current->order, current->registration_id);
        });
    }
    const auto weak_self = weak_from_this();
    const std::weak_ptr<AudioEntry> weak_entry = entry;
    return std::make_shared<ScopedSubscription>(
        [weak_self, weak_entry, id = entry->registration_id] {
            if (const auto active_entry = weak_entry.lock()) {
                active_entry->active.store(false, std::memory_order_release);
            }
            if (const auto self = weak_self.lock()) {
                self->UnregisterAudio(id);
            }
        });
}

MediaSubmitResult CapturedMediaPipeline::SubmitVideo(
    const std::shared_ptr<const CapturedVideoFrame>& frame) {
    video_received_.fetch_add(1, std::memory_order_relaxed);
    if (!frame || !frame->Payload()) {
        video_failed_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(PipelineError(
            RenderErrorCode::kPipelineInvalidFrame,
            "submit_video",
            "captured video frame or payload is missing"));
    }
    auto current = frame;
    for (const auto& entry : VideoSnapshot()) {
        if (!entry->active.load(std::memory_order_acquire)) {
            continue;
        }
        const auto processor = entry->callback.lock();
        if (!processor ||
            !entry->active.load(std::memory_order_acquire)) {
            continue;
        }
        try {
            auto result = (*processor)(current);
            if (!result) {
                video_failed_.fetch_add(1, std::memory_order_relaxed);
                return std::unexpected(std::move(result.error()));
            }
            current = std::move(*result);
        }
        catch (const std::exception& error) {
            video_failed_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(PipelineError(
                RenderErrorCode::kPipelineProcessorFailed,
                "process_video",
                "processor=" + entry->id + " exception=" + error.what()));
        }
        catch (...) {
            video_failed_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(PipelineError(
                RenderErrorCode::kPipelineProcessorFailed,
                "process_video",
                "processor=" + entry->id + " exception=unknown"));
        }
        if (!current) {
            video_dropped_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
    }
    auto delivered = video_output_(current);
    if (delivered) {
        video_delivered_.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        video_failed_.fetch_add(1, std::memory_order_relaxed);
    }
    return delivered;
}

MediaSubmitResult CapturedMediaPipeline::SubmitAudio(
    const std::shared_ptr<const CapturedAudioFrame>& frame) {
    audio_received_.fetch_add(1, std::memory_order_relaxed);
    if (!frame || !frame->payload) {
        audio_failed_.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(PipelineError(
            RenderErrorCode::kPipelineInvalidFrame,
            "submit_audio",
            "captured audio frame or payload is missing"));
    }
    auto current = frame;
    for (const auto& entry : AudioSnapshot()) {
        if (!entry->active.load(std::memory_order_acquire)) {
            continue;
        }
        const auto processor = entry->callback.lock();
        if (!processor ||
            !entry->active.load(std::memory_order_acquire)) {
            continue;
        }
        try {
            auto result = (*processor)(current);
            if (!result) {
                audio_failed_.fetch_add(1, std::memory_order_relaxed);
                return std::unexpected(std::move(result.error()));
            }
            current = std::move(*result);
        }
        catch (const std::exception& error) {
            audio_failed_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(PipelineError(
                RenderErrorCode::kPipelineProcessorFailed,
                "process_audio",
                "processor=" + entry->id + " exception=" + error.what()));
        }
        catch (...) {
            audio_failed_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(PipelineError(
                RenderErrorCode::kPipelineProcessorFailed,
                "process_audio",
                "processor=" + entry->id + " exception=unknown"));
        }
        if (!current) {
            audio_dropped_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
    }
    auto delivered = audio_output_(current);
    if (delivered) {
        audio_delivered_.fetch_add(1, std::memory_order_relaxed);
    }
    else {
        audio_failed_.fetch_add(1, std::memory_order_relaxed);
    }
    return delivered;
}

bool CapturedMediaPipeline::HasVideoProcessors() const {
    std::lock_guard lock(mutex_);
    return std::ranges::any_of(video_processors_, [](const auto& entry) {
        return entry->active.load(std::memory_order_acquire) &&
               !entry->callback.expired();
    });
}

bool CapturedMediaPipeline::HasAudioProcessors() const {
    std::lock_guard lock(mutex_);
    return std::ranges::any_of(audio_processors_, [](const auto& entry) {
        return entry->active.load(std::memory_order_acquire) &&
               !entry->callback.expired();
    });
}

CapturedMediaPipelineSnapshot CapturedMediaPipeline::Snapshot() const {
    std::lock_guard lock(mutex_);
    const auto active_video = std::ranges::count_if(
        video_processors_, [](const auto& entry) {
            return entry->active.load(std::memory_order_acquire) &&
                   !entry->callback.expired();
        });
    const auto active_audio = std::ranges::count_if(
        audio_processors_, [](const auto& entry) {
            return entry->active.load(std::memory_order_acquire) &&
                   !entry->callback.expired();
        });
    return CapturedMediaPipelineSnapshot{
        .video_processors = static_cast<std::size_t>(active_video),
        .audio_processors = static_cast<std::size_t>(active_audio),
        .video_received = video_received_.load(std::memory_order_relaxed),
        .video_delivered = video_delivered_.load(std::memory_order_relaxed),
        .video_dropped = video_dropped_.load(std::memory_order_relaxed),
        .video_failed = video_failed_.load(std::memory_order_relaxed),
        .audio_received = audio_received_.load(std::memory_order_relaxed),
        .audio_delivered = audio_delivered_.load(std::memory_order_relaxed),
        .audio_dropped = audio_dropped_.load(std::memory_order_relaxed),
        .audio_failed = audio_failed_.load(std::memory_order_relaxed),
    };
}

void CapturedMediaPipeline::UnregisterVideo(
    const std::uint64_t registration_id) {
    std::lock_guard lock(mutex_);
    std::erase_if(video_processors_, [registration_id](const auto& entry) {
        return entry->registration_id == registration_id;
    });
}

void CapturedMediaPipeline::UnregisterAudio(
    const std::uint64_t registration_id) {
    std::lock_guard lock(mutex_);
    std::erase_if(audio_processors_, [registration_id](const auto& entry) {
        return entry->registration_id == registration_id;
    });
}

std::vector<std::shared_ptr<CapturedMediaPipeline::VideoEntry>>
CapturedMediaPipeline::VideoSnapshot() {
    std::lock_guard lock(mutex_);
    std::erase_if(video_processors_, [](const auto& entry) {
        return !entry->active.load(std::memory_order_acquire) ||
               entry->callback.expired();
    });
    return video_processors_;
}

std::vector<std::shared_ptr<CapturedMediaPipeline::AudioEntry>>
CapturedMediaPipeline::AudioSnapshot() {
    std::lock_guard lock(mutex_);
    std::erase_if(audio_processors_, [](const auto& entry) {
        return !entry->active.load(std::memory_order_acquire) ||
               entry->callback.expired();
    });
    return audio_processors_;
}

}  // namespace px::render
