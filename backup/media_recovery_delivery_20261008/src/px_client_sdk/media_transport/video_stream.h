#pragma once

#include "media_wire.h"
#include "video_receive_queue.h"
#include <map>
#include <string>

namespace px::media {
enum class VideoCodec : std::uint8_t { kH264 = 1, kH265 = 2 };
struct VideoFrame final {
    VideoCodec codec{VideoCodec::kH264};
    VideoFrameKind kind{VideoFrameKind::kPredicted};
    std::uint8_t stream{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint64_t frame_index{};
    std::string monitor{};
    Packet encoded{};
};
[[nodiscard]] std::optional<PacketizedVideo> PacketizeVideoFrame(const VideoFrame& frame, VideoPacketParameters parameters);
struct VideoStreamOutput final {
    std::optional<VideoReceiveStatistics> statistics{};
    std::optional<VideoFrame> frame{};
    std::vector<VideoLoss> losses{};
    std::optional<std::uint64_t> invalid_reference_frame{};
    std::size_t recovered{};
    bool needs_idr{};
    bool rejected{};
};
class VideoStreamReceiver final {
  public:
    [[nodiscard]] VideoStreamOutput Feed(const MediaDatagram& datagram, std::uint64_t now_us);
    void Reset();

  private:
    struct Stream final {
        explicit Stream(std::uint16_t size) : packet_size(size), queue(size) {}
        std::uint16_t packet_size{};
        VideoReceiveQueue queue;
        std::optional<std::uint32_t> last_delivered{};
        std::optional<std::uint64_t> last_encoder_frame{};
    };
    std::map<std::uint8_t, Stream> streams_{};
};
} // namespace px::media
