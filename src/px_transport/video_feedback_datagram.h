#pragma once

#include "video_receive_feedback.h"

namespace px::transport {

// Cumulative, replaceable status, like Moonlight's unsequenced frame FEC reports.
// Missing/reordered reports are harmless; recovery requests remain on reliable streams.
[[nodiscard]] inline media::Packet EncodeVideoFeedbackDatagram(const VideoReceiveFeedback& report) {
    media::Packet packet(40, 0);
    packet[0] = 'P';
    packet[1] = 'X';
    packet[2] = 'F';
    packet[3] = 1;
    packet[4] = report.stream;
    packet[5] = report.latest_frame_index.has_value() ? 1 : 0;
    media::wire::Put(packet, 8, report.elapsed_us, 8);
    media::wire::Put(packet, 16, report.complete_frames, 8);
    media::wire::Put(packet, 24, report.complete_bytes, 8);
    media::wire::Put(packet, 32, report.latest_frame_index.value_or(0), 8);
    return packet;
}

[[nodiscard]] inline std::optional<VideoReceiveFeedback> DecodeVideoFeedbackDatagram(std::span<const std::uint8_t> packet) {
    if (packet.size() != 40 || packet[0] != 'P' || packet[1] != 'X' || packet[2] != 'F' || packet[3] != 1 || packet[5] > 1 || packet[6] || packet[7])
        return std::nullopt;
    VideoReceiveFeedback report{.stream = packet[4],
                                .elapsed_us = media::wire::Get(packet, 8, 8),
                                .complete_frames = media::wire::Get(packet, 16, 8),
                                .complete_bytes = media::wire::Get(packet, 24, 8)};
    if (packet[5]) report.latest_frame_index = media::wire::Get(packet, 32, 8);
    return report;
}
}  // namespace px::transport
