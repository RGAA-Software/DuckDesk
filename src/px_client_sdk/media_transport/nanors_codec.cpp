#include "nanors_codec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <mutex>
#include <rs.h>

namespace px::media {
namespace {
struct CodecCloser final {
    void operator()(reed_solomon* codec) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): synchronous nanors C ABI deleter.
        reed_solomon_release(codec);
    }
};
using CodecOwner = std::unique_ptr<reed_solomon, CodecCloser>;

bool Valid(const std::vector<Packet>& shards, std::size_t data_count) {
    return data_count > 0 && data_count < shards.size() && shards.size() <= DATA_SHARDS_MAX && !shards.front().empty() &&
           shards.front().size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           std::ranges::all_of(shards, [size = shards.front().size()](const Packet& shard) { return shard.size() == size; });
}

CodecOwner CreateCodec(std::size_t data_count, std::size_t total, FecProfile profile) {
    static std::once_flag initialization{};
    std::call_once(initialization, []() { reed_solomon_init(); });
    if (profile == FecProfile::kAudio4Plus2 && (data_count != 4 || total != 6))
        return {};
    CodecOwner codec{reed_solomon_new(static_cast<int>(data_count), static_cast<int>(total - data_count))};
    if (codec && profile == FecProfile::kAudio4Plus2) {
        // Sunshine audioBroadcastThread and Moonlight RtpaInitializeQueue use this OpenFEC matrix.
        constexpr std::array<std::uint8_t, 8> parity{0x77, 0x40, 0x38, 0x0e, 0xc7, 0xa7, 0x0d, 0x6c};
        std::ranges::copy(parity, codec->p);
    }
    return codec;
}
} // namespace

bool NanorsCodec::Encode(std::vector<Packet>& shards, std::size_t data_count, FecProfile profile) {
    if (!Valid(shards, data_count))
        return false;
    const auto codec = CreateCodec(data_count, shards.size(), profile);
    if (!codec)
        return false;
    // Required borrowed C ABI pointer table; it never survives this synchronous call.
    std::array<uint8_t*, DATA_SHARDS_MAX> addresses{}; // NOLINT(gammaray-raw-pointer-boundary): nanors C ABI shard table.
    for (std::size_t index{}; index < shards.size(); ++index)
        addresses[index] = shards[index].data();
    return reed_solomon_encode(codec.get(), addresses.data(), static_cast<int>(shards.size()), static_cast<int>(shards.front().size())) == 0;
}

bool NanorsCodec::Decode(std::vector<Packet>& shards, std::span<const std::uint8_t> missing, std::size_t data_count, FecProfile profile) {
    if (!Valid(shards, data_count) || missing.size() != shards.size())
        return false;
    if (std::ranges::count_if(missing, [](std::uint8_t value) { return value != 0; }) > static_cast<std::ptrdiff_t>(shards.size() - data_count)) {
        return false;
    }
    const auto codec = CreateCodec(data_count, shards.size(), profile);
    if (!codec)
        return false;
    std::vector<std::uint8_t> marks(missing.begin(), missing.end());
    std::array<uint8_t*, DATA_SHARDS_MAX> addresses{}; // NOLINT(gammaray-raw-pointer-boundary): synchronous nanors C ABI shard table.
    for (std::size_t index{}; index < shards.size(); ++index)
        addresses[index] = shards[index].data();
    return reed_solomon_decode(codec.get(), addresses.data(), marks.data(), static_cast<int>(shards.size()),
                               static_cast<int>(shards.front().size())) == 0;
}
} // namespace px::media
