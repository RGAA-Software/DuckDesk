#pragma once

#include <algorithm>
#include <array>

#include "media_transport/media_wire.h"

namespace px::transport {

// The FEC adapter pads every Opus shard to a fixed length. Keep the exact FEC bytes/sequence,
// but omit its trailing zeros on the wire and restore them before the existing jitter queue.
// This applies to data and parity, on both paths, so path changes never change audio state.
inline media::Packet CompactAudioDatagram(std::span<const std::uint8_t> packet) {
    const auto parsed = media::ParseMedia(packet);
    if (!parsed || parsed->kind != media::MediaKind::kAudio) return {};
    auto trimmed_size = packet.size();
    while (trimmed_size > media::kEnvelopeSize && packet[trimmed_size - 1] == 0) --trimmed_size;
    media::Packet compact{'P', 'X', 'A', 1, 0, 0, 0, 0};
    media::wire::Put(compact, 4, packet.size(), 2);
    compact.insert(compact.end(), packet.begin() + media::kEnvelopeSize, packet.begin() + trimmed_size);
    return compact;
}

inline std::optional<media::Packet> ExpandAudioDatagram(std::span<const std::uint8_t> packet) {
    constexpr std::array<std::uint8_t, 4> kHeader{'P', 'X', 'A', 1};
    if (packet.size() < media::kEnvelopeSize || packet.size() > 1500 || !std::equal(kHeader.begin(), kHeader.end(), packet.begin()) ||
        packet[6] != 0 || packet[7] != 0)
        return std::nullopt;
    const auto expanded_size = media::wire::Get(packet, 4, 2);
    if (expanded_size < packet.size() || expanded_size <= media::kEnvelopeSize + 12 || expanded_size > 1500) return std::nullopt;
    media::Packet expanded{'P', 'X', 'M', 2, static_cast<std::uint8_t>(media::MediaKind::kAudio), 0, 0, 0};
    expanded.insert(expanded.end(), packet.begin() + media::kEnvelopeSize, packet.end());
    expanded.resize(static_cast<std::size_t>(expanded_size), 0);
    return expanded;
}
}  // namespace px::transport
