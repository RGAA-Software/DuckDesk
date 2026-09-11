#include "video_stream.h"
#include <algorithm>

namespace px::media {
namespace {
constexpr std::size_t kDescriptorSize = 28;
constexpr std::size_t kRtpSize = 32;
constexpr std::size_t kShortHeaderSize = 8;
} // namespace

std::optional<PacketizedVideo> PacketizeVideoFrame(const VideoFrame& frame, VideoPacketParameters parameters) {
    if (frame.encoded.empty() || frame.encoded.size() > 8 * 1024 * 1024 || frame.monitor.size() > 255 || frame.width == 0 || frame.height == 0 ||
        parameters.datagram_size < 576 || parameters.datagram_size > 1500)
        return std::nullopt;
    Packet payload(kDescriptorSize, 0);
    payload[0] = 'P';
    payload[1] = 'V';
    payload[2] = 2;
    payload[3] = static_cast<std::uint8_t>(frame.codec);
    payload[4] = frame.stream;
    payload[5] = static_cast<std::uint8_t>(frame.monitor.size());
    wire::Put(payload, 6, frame.width, 2);
    wire::Put(payload, 8, frame.height, 2);
    wire::Put(payload, 12, frame.frame_index, 8);
    wire::Put(payload, 20, frame.encoded.size(), 4);
    wire::Put(payload, 24, parameters.frame_index, 4);
    payload.insert(payload.end(), frame.monitor.begin(), frame.monitor.end());
    payload.insert(payload.end(), frame.encoded.begin(), frame.encoded.end());
    // Capture/encoder timestamps can skip. The caller supplies a separate contiguous transport frame counter.
    parameters.kind = frame.kind;
    parameters.datagram_size -= kEnvelopeSize;
    auto result = PacketizeVideo(payload, parameters);
    if (result) {
        for (auto& packet : result->packets)
            packet = WrapMedia(MediaKind::kVideo, frame.stream, packet);
    }
    return result;
}

void VideoStreamReceiver::Reset() {
    streams_.clear();
}
VideoStreamOutput VideoStreamReceiver::Feed(const MediaDatagram& datagram, std::uint64_t now_us) {
    VideoStreamOutput output{};
    if (datagram.kind != MediaKind::kVideo || datagram.payload.size() < 568 || datagram.payload.size() > 1492) {
        output.rejected = true;
        return output;
    }
    auto [position, inserted] = streams_.try_emplace(datagram.stream, static_cast<std::uint16_t>(datagram.payload.size()));
    auto& stream = position->second;
    if (stream.packet_size != datagram.payload.size()) {
        output.rejected = true;
        return output;
    }
    auto queued = stream.queue.Feed(datagram.payload, now_us);
    // Joining an already running encoder does not mean its earlier, never-observed frames were lost on this connection.
    // Do not invalidate encoder timestamps before this decoder has established its first reference point.
    if (stream.last_delivered) {
        output.losses = std::move(queued.losses);
        if (!output.losses.empty() && stream.last_encoder_frame)
            output.invalid_reference_frame = *stream.last_encoder_frame + 1;
    }
    output.recovered = queued.recovered_data;
    output.rejected = queued.malformed;
    if (queued.data_packets.empty())
        return output;
    Packet payload{};
    for (const auto& packet : queued.data_packets)
        payload.insert(payload.end(), packet.begin() + kRtpSize, packet.end());
    const auto last_size = static_cast<std::size_t>(payload[4] | (static_cast<unsigned>(payload[5]) << 8));
    const auto block_payload = stream.packet_size - kRtpSize;
    if (last_size == 0 || last_size > block_payload || payload[0] != 1) {
        output.rejected = output.needs_idr = true;
        return output;
    }
    payload.resize(payload.size() - block_payload + last_size);
    const auto kind = static_cast<VideoFrameKind>(payload[3]);
    if (payload.size() < kShortHeaderSize + kDescriptorSize ||
        (kind != VideoFrameKind::kIdr && kind != VideoFrameKind::kPredicted && kind != VideoFrameKind::kReferenceRecovery)) {
        output.rejected = output.needs_idr = true;
        return output;
    }
    const auto descriptor = std::span<const std::uint8_t>{payload}.subspan(kShortHeaderSize);
    const auto name_size = static_cast<std::size_t>(descriptor[5]);
    const auto encoded_size = wire::Get(descriptor, 20, 4);
    if (descriptor[0] != 'P' || descriptor[1] != 'V' || descriptor[2] != 2 || (descriptor[3] != 1 && descriptor[3] != 2) ||
        descriptor[4] != datagram.stream || descriptor[10] != 0 || descriptor[11] != 0 ||
        descriptor.size() != kDescriptorSize + name_size + encoded_size || encoded_size == 0 || wire::Get(descriptor, 6, 2) == 0 ||
        wire::Get(descriptor, 8, 2) == 0) {
        output.rejected = output.needs_idr = true;
        return output;
    }
    const auto frame_index = wire::Get(descriptor, 12, 8);
    const auto wire_index = static_cast<std::uint32_t>(wire::Get(descriptor, 24, 4));
    const auto& first_packet = queued.data_packets.front();
    const auto packet_frame = static_cast<std::uint32_t>(first_packet[20]) | (static_cast<std::uint32_t>(first_packet[21]) << 8) |
                              (static_cast<std::uint32_t>(first_packet[22]) << 16) | (static_cast<std::uint32_t>(first_packet[23]) << 24);
    if (wire_index != packet_frame) {
        output.rejected = output.needs_idr = true;
        return output;
    }
    // A speculative loss does not poison a frame that was subsequently recovered. Only complete delivery advances the reference chain.
    if (kind != VideoFrameKind::kIdr &&
        (!stream.last_delivered || (kind != VideoFrameKind::kReferenceRecovery && *stream.last_delivered + 1 != wire_index))) {
        // A known decoder reference can be repaired by RFI. Do not replace this with a throttled IDR on every dependent frame.
        // Retrying on subsequent complete frames also covers a lost UDP recovery request without changing the WS control channel.
        if (stream.last_encoder_frame)
            output.invalid_reference_frame = *stream.last_encoder_frame + 1;
        else
            output.needs_idr = true;
        return output;
    }
    VideoFrame frame{};
    frame.codec = static_cast<VideoCodec>(descriptor[3]);
    frame.kind = kind;
    frame.stream = datagram.stream;
    frame.width = static_cast<std::uint16_t>(wire::Get(descriptor, 6, 2));
    frame.height = static_cast<std::uint16_t>(wire::Get(descriptor, 8, 2));
    frame.frame_index = frame_index;
    frame.monitor.assign(descriptor.begin() + kDescriptorSize, descriptor.begin() + kDescriptorSize + name_size);
    frame.encoded.assign(descriptor.begin() + kDescriptorSize + name_size, descriptor.end());
    stream.last_delivered = wire_index;
    stream.last_encoder_frame = frame_index;
    output.frame = std::move(frame);
    return output;
}
} // namespace px::media
