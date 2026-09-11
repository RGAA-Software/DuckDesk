#pragma once

#include "nanors_codec.h"
#include <optional>

namespace px::media {
enum class MediaKind : std::uint8_t { kVideo = 1, kAudio = 2 };
constexpr std::size_t kEnvelopeSize = 8;
struct MediaDatagram final {
    MediaKind kind{MediaKind::kVideo};
    std::uint8_t stream{};
    std::span<const std::uint8_t> payload{}; // Synchronous parser view; never retained by a receive queue.
};
[[nodiscard]] Packet WrapMedia(MediaKind kind, std::uint8_t stream, std::span<const std::uint8_t> packet);
[[nodiscard]] std::optional<MediaDatagram> ParseMedia(std::span<const std::uint8_t> packet);

namespace wire {
inline void Put(Packet& packet, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t index{}; index < width; ++index)
        packet[offset + index] = static_cast<std::uint8_t>(value >> (8 * (width - index - 1)));
}
inline std::uint64_t Get(std::span<const std::uint8_t> packet, std::size_t offset, std::size_t width) {
    std::uint64_t value{};
    for (std::size_t index{}; index < width; ++index)
        value = (value << 8) | packet[offset + index];
    return value;
}
} // namespace wire
} // namespace px::media
