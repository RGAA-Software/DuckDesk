#include "media_wire.h"
#include <algorithm>

namespace px::media {
Packet WrapMedia(MediaKind kind, std::uint8_t stream, std::span<const std::uint8_t> packet) {
    if (packet.empty() || packet.size() + kEnvelopeSize > 1500)
        return {};
    Packet result{'P', 'X', 'M', 2, static_cast<std::uint8_t>(kind), stream, 0, 0};
    result.insert(result.end(), packet.begin(), packet.end());
    return result;
}
std::optional<MediaDatagram> ParseMedia(std::span<const std::uint8_t> packet) {
    if (packet.size() <= kEnvelopeSize || packet.size() > 1500 || packet[0] != 'P' || packet[1] != 'X' || packet[2] != 'M' || packet[3] != 2 ||
        packet[6] != 0 || packet[7] != 0 || (packet[4] != 1 && packet[4] != 2) || (packet[4] == 2 && packet[5] != 0))
        return std::nullopt;
    return MediaDatagram{static_cast<MediaKind>(packet[4]), packet[5], packet.subspan(kEnvelopeSize)};
}
} // namespace px::media
