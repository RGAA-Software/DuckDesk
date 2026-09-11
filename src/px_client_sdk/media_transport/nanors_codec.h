#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace px::media {
using Packet = std::vector<std::uint8_t>;
enum class FecProfile { kVideo, kAudio4Plus2 };

// Equal-sized, owned buffers. No third-party pointers escape the synchronous ABI adapter.
class NanorsCodec final {
  public:
    static bool Encode(std::vector<Packet>& shards, std::size_t data_count, FecProfile profile = FecProfile::kVideo);
    static bool Decode(std::vector<Packet>& shards, std::span<const std::uint8_t> missing, std::size_t data_count,
                       FecProfile profile = FecProfile::kVideo);
};
} // namespace px::media
