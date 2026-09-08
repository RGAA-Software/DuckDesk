#include "sdk_recording_session.h"
#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/uuid.h"
#include "px_message.pb.h"
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace px {
namespace {
std::span<const std::uint8_t> EncodedBytes(const std::string& bytes) {
    // NOLINT(gammaray-raw-pointer-boundary): synchronous view of an owning protobuf string.
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

std::optional<RecordVideoCodec> RecordingCodec(VideoType type) {
    if (type == kNetH264)
        return RecordVideoCodec::kH264;
    if (type == kNetHevc)
        return RecordVideoCodec::kH265;
    return {};
}
} // namespace

struct RecordingSession::State final {
    State(RecordingSessionConfig settings, RecordingSessionCallbacks handlers)
        : config(std::move(settings)), callbacks(std::move(handlers)), completion(promise.get_future().share()) {}
    const RecordingSessionConfig config;
    const std::string run_id{GetUUID()};
    const RecordingSessionCallbacks callbacks;
    std::mutex mutex{};
    std::condition_variable available{};
    struct QueuedPacket final {
        std::shared_ptr<const Message> message{};
        std::size_t bytes{};
    };
    std::deque<QueuedPacket> packets{};
    std::size_t queued_bytes{};
    bool started{};
    bool accepting{};
    bool stopping{};
    bool completed{};
    std::thread::id worker_id{};
    std::string ingress_error{};
    std::promise<RecordingSessionResult> promise{};
    const std::shared_future<RecordingSessionResult> completion;
};

std::shared_ptr<RecordingSession> RecordingSession::Create(RecordingSessionConfig config, RecordingSessionCallbacks callbacks) {
    if (config.writer.dir.empty() || config.monitor_count == 0 || config.monitor_count > 16 || config.queue_byte_limit == 0 ||
        config.queue_packet_limit == 0)
        return {};
    return std::make_shared<RecordingSession>(ConstructionToken{}, std::move(config), std::move(callbacks));
}

RecordingSession::RecordingSession(ConstructionToken, RecordingSessionConfig config, RecordingSessionCallbacks callbacks)
    : state_(std::make_shared<State>(std::move(config), std::move(callbacks))) {}

RecordingSession::~RecordingSession() {
    Stop();
    std::thread retiring{};
    {
        std::lock_guard lock(thread_mutex_);
        retiring = std::move(worker_);
    }
    if (retiring.joinable()) {
        if (retiring.get_id() == std::this_thread::get_id())
            PxAsyncRuntime::DeferJoin(std::move(retiring));
        else
            retiring.join();
    }
}

bool RecordingSession::Start() {
    std::lock_guard thread_lock(thread_mutex_);
    std::lock_guard lock(state_->mutex);
    if (state_->started || state_->stopping)
        return false;
    try {
        worker_ = std::thread([state = state_] { Run(state); });
        state_->started = true;
        state_->accepting = true;
        return true;
    } catch (const std::exception&) {
        state_->stopping = true;
        state_->completed = true;
        state_->promise.set_value({.error = "recording_worker_start_failed"});
        return false;
    }
}

RecordingSubmitResult RecordingSession::Submit(std::shared_ptr<const Message> message) {
    if (!message)
        return RecordingSubmitResult::kIgnored;
    std::string invalid{};
    if (message->type() == kVideoFrame && message->has_video_frame()) {
        const auto& video = message->video_frame();
        if (video.data().empty())
            return RecordingSubmitResult::kIgnored;
        if (!RecordingCodec(video.type()) || video.frame_width() <= 0 || video.frame_height() <= 0)
            invalid = "unsupported_recording_video";
    } else if (message->type() == kAudioFrame && message->has_audio_frame()) {
        const auto& audio = message->audio_frame();
        // Preserve ordered UDP loss duration, without treating the marker as an encoded Opus packet.
        if (audio.data().empty() && audio.extra() != "udp_lost")
            return RecordingSubmitResult::kIgnored;
        const auto samples = audio.frame_size();
        const bool valid_duration = samples == 120 || samples == 240 || samples == 480 || samples == 960 || samples == 1920 || samples == 2880;
        if (audio.samples() != 48000 || audio.channels() != 2 || audio.bits() != 16 || !valid_duration || audio.data().size() > 1275)
            invalid = "unsupported_recording_audio";
    } else {
        return RecordingSubmitResult::kIgnored;
    }
    const auto bytes = message->ByteSizeLong();
    std::lock_guard lock(state_->mutex);
    if (!state_->accepting)
        return RecordingSubmitResult::kStopped;
    if (!invalid.empty() || state_->packets.size() >= state_->config.queue_packet_limit ||
        bytes > state_->config.queue_byte_limit - state_->queued_bytes) {
        const auto result = invalid.empty() ? RecordingSubmitResult::kQueueFull : RecordingSubmitResult::kInvalid;
        state_->ingress_error = invalid.empty() ? "recording_queue_full" : std::move(invalid);
        state_->accepting = false;
        state_->stopping = true;
        state_->available.notify_one();
        return result;
    }
    state_->queued_bytes += bytes;
    state_->packets.push_back({std::move(message), bytes});
    state_->available.notify_one();
    return RecordingSubmitResult::kAccepted;
}

void RecordingSession::Stop() {
    std::lock_guard lock(state_->mutex);
    state_->accepting = false;
    state_->stopping = true;
    if (!state_->started && !state_->completed) {
        state_->completed = true;
        state_->promise.set_value({});
    }
    state_->available.notify_one();
}

bool RecordingSession::WaitFor(std::chrono::milliseconds timeout) const {
    {
        std::lock_guard lock(state_->mutex);
        if (state_->worker_id == std::this_thread::get_id())
            return false;
    }
    return state_->completion.wait_for(timeout) == std::future_status::ready;
}

std::shared_future<RecordingSessionResult> RecordingSession::Completion() const {
    return state_->completion;
}

void RecordingSession::Run(std::shared_ptr<State> state) {
    {
        std::lock_guard lock(state->mutex);
        state->worker_id = std::this_thread::get_id();
    }
    RecordingSessionResult result{};
    std::map<std::size_t, std::shared_ptr<RecordWriter>> writers{};
    try {
        if (state->callbacks.started)
            state->callbacks.started();
        for (;;) {
            std::shared_ptr<const Message> message{};
            {
                std::unique_lock lock(state->mutex);
                state->available.wait(lock, [state] { return state->stopping || !state->packets.empty(); });
                if (state->packets.empty()) {
                    result.error = state->ingress_error;
                    break;
                }
                message = std::move(state->packets.front().message);
                state->queued_bytes -= state->packets.front().bytes;
                state->packets.pop_front();
            }
            ++result.processed_packets;
            if (message->type() == kVideoFrame) {
                const auto& video = message->video_frame();
                // Android consumes one selected remote monitor, whose remote index need not be zero.
                const auto index = state->config.monitor_count == 1 ? 0U : static_cast<std::size_t>(video.mon_index());
                if (index >= state->config.monitor_count)
                    continue;
                auto& writer = writers[index];
                if (!writer) {
                    auto config = state->config.writer;
                    if (state->config.monitor_count > 1)
                        config.monitor_name = "mon" + std::to_string(index);
                    config.monitor_name += "_" + state->run_id;
                    writer = RecordWriter::Make(config);
                    if (!writer)
                        throw std::runtime_error("recording_writer_create_failed");
                }
                writer->OnEncodedVideo(EncodedBytes(video.data()), *RecordingCodec(video.type()), video.frame_width(), video.frame_height(),
                                       video.key());
                if (!writer->Error().empty())
                    throw std::runtime_error(writer->Error());
                ++result.video_packets;
            } else {
                const auto& audio = message->audio_frame();
                for (const auto& [index, writer] : writers) {
                    static_cast<void>(index);
                    writer->OnEncodedAudio(EncodedBytes(audio.data()), audio.frame_size());
                    if (!writer->Error().empty())
                        throw std::runtime_error(writer->Error());
                }
                if (!writers.empty()) {
                    if (audio.data().empty())
                        ++result.audio_gap_packets;
                    else
                        ++result.audio_packets;
                }
            }
        }
    } catch (const std::exception& error) {
        result.error = error.what();
    } catch (...) {
        result.error = "recording_worker_failed";
    }
    {
        std::lock_guard lock(state->mutex);
        state->accepting = false;
        state->stopping = true;
        state->packets.clear();
        state->queued_bytes = 0;
    }
    std::uint64_t completed_segments{};
    for (const auto& [index, writer] : writers) {
        static_cast<void>(index);
        if (!writer)
            continue;
        try {
            writer->Stop();
            completed_segments += writer->CompletedSegments();
            if (result.error.empty())
                result.error = writer->Error();
        } catch (const std::exception& error) {
            if (result.error.empty())
                result.error = error.what();
        } catch (...) {
            if (result.error.empty())
                result.error = "recording_finalize_failed";
        }
    }
    if (completed_segments != 0)
        result.directories.push_back(state->config.writer.dir);
    else if (result.video_packets != 0 && result.error.empty())
        result.error = "recording_no_keyframe";
    writers.clear(); // FFmpeg resources are destroyed on the same worker before completion is published.
    try {
        if (state->callbacks.finished)
            state->callbacks.finished(result);
    } catch (...) {
        LOGE("Recording completion callback failed");
    }
    state->promise.set_value(std::move(result));
    {
        std::lock_guard lock(state->mutex);
        state->completed = true;
    }
}
} // namespace px
