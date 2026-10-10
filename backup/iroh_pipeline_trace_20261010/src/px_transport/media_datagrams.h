#pragma once

#include <asio2/external/asio.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

#include "media_timer_resolution.h"
#include "media_transport/audio_stream.h"
#include "media_transport/video_recovery_policy.h"
#include "transport.h"
#include "video_flight_window.h"
#include "video_receive_feedback.h"
#include "voice_datagram.h"

namespace px {
class Thread;
}

namespace px::transport {

// A fixed conservative size fits both direct QUIC and Relay paths. Do not grow shards on a larger path:
// the same FEC stream must remain decodable when the selected connection path changes.
inline constexpr std::uint16_t kMediaDatagramBytes{1100};

struct MediaSendOptions final {
    std::chrono::milliseconds frame_deadline{250};
    std::uint8_t fec_percent{20};
};

class MediaDatagramSender final : public std::enable_shared_from_this<MediaDatagramSender> {
public:
    MediaDatagramSender(std::shared_ptr<Connection> connection, asio::any_io_executor executor, MediaSendOptions options = {});
    ~MediaDatagramSender();
    // False means no work was accepted. An accepted frame invokes completion once, including cancellation.
    [[nodiscard]] bool SendVideo(const media::VideoFrame& frame, std::function<void(bool)> completion);
    [[nodiscard]] bool SendAudio(std::span<const std::uint8_t> opus);
    [[nodiscard]] bool CanEncodeVideo(std::uint8_t stream);
    void ObserveFeedback(const VideoReceiveFeedback& report);
    void Stop();

private:
    struct PendingVideo final {
        std::vector<media::Packet> packets{};
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point deadline{};
        std::size_t bytes{};
        std::function<void(bool)> completion{};
    };
    void StartNextVideo();
    struct VideoSequence final {
        std::uint32_t frame{1};
        std::uint16_t packet{};
    };
    struct FeedbackTrace final {
        std::optional<std::chrono::steady_clock::time_point> arrived{};
        std::optional<std::chrono::steady_clock::time_point> blocked_since{};
        std::uint64_t elapsed_us{};
        bool block_logged{};
    };
    std::shared_ptr<Connection> connection_{};
    MediaTimerResolution timer_resolution_{};
    asio::any_io_executor executor_{};
    MediaSendOptions options_{};
    std::mutex video_mutex_{};
    std::mutex audio_mutex_{};
    std::map<std::uint8_t, VideoSequence> sequences_{};
    std::map<std::uint8_t, std::chrono::steady_clock::time_point> last_encoded_frame_{};
    VideoFlightWindow flight_{};
    std::map<std::uint8_t, FeedbackTrace> feedback_traces_{};
    media::AudioPacketizer audio_{};
    std::atomic_bool stopped_{};
    std::deque<PendingVideo> pending_video_{};
    bool video_active_{};
    std::size_t pending_video_bytes_{};
    std::chrono::steady_clock::time_point window_started_{};
    std::chrono::steady_clock::time_point next_path_sample_{};
    PathKind current_path_{PathKind::kUnknown};
    std::uint64_t offered_frames_{};
    std::uint64_t offered_idr_frames_{};
    std::uint64_t busy_frames_{};
    std::uint64_t flight_limited_frames_{};
    std::uint64_t encoded_bytes_{};
    std::atomic_uint64_t video_datagram_bytes_{};
    std::atomic_uint64_t audio_datagram_bytes_{};
    std::atomic_uint64_t opus_bytes_{};
    std::atomic_uint64_t completed_frames_{};
    std::atomic_uint64_t failed_frames_{};
    std::atomic_uint64_t maximum_send_us_{};
};

struct MediaReceiveCallbacks final {
    std::function<void(media::VideoFrame)> video{};
    std::function<void(media::AudioDelivery)> audio{};
    std::function<void(std::shared_ptr<Message>)> voice{};
    std::function<void(media::VideoRecoveryRequest)> recovery{};
    std::function<void(VideoReceiveFeedback)> feedback{};
    std::function<void()> closed{};
};

// Reuses the existing FEC, reference recovery and Opus jitter queue. One receiver consumes datagrams
// for this connection; reliable messages continue on MessageSession's independent workers.
class MediaDatagramReceiver final : public std::enable_shared_from_this<MediaDatagramReceiver> {
public:
    MediaDatagramReceiver(std::shared_ptr<Connection> connection, MediaReceiveCallbacks callbacks);
    ~MediaDatagramReceiver();
    [[nodiscard]] bool Start();
    void Stop();

private:
    static void Receive(std::weak_ptr<MediaDatagramReceiver> owner, std::shared_ptr<Connection> connection);
    void Feed(std::span<const std::uint8_t> payload, std::uint64_t now_us);
    void PollRecovery(std::uint64_t now_us);
    std::shared_ptr<Connection> connection_{};
    MediaReceiveCallbacks callbacks_{};
    media::VideoStreamReceiver video_{};
    media::AudioReceiveQueue audio_{};
    media::VideoRecoveryPolicy recovery_{};
    VideoReceiveProgress receive_progress_{};
    std::map<std::uint8_t, std::uint64_t> statistics_logged_us_{};
    std::map<std::uint8_t, std::uint64_t> feedback_elapsed_us_{};
    std::mutex mutex_{};
    std::shared_ptr<Thread> worker_{};
    std::atomic_bool stopped_{true};
    bool started_once_{};
};
}  // namespace px::transport
