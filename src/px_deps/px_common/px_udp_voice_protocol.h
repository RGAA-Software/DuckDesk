#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "px_udp_protocol.h"
#include "udp_voice_frame.h"

namespace px {

// Common UDP header + association length(u8), call length(u8), Opus length(u16),
// sequence(u32), capture time(u64), then association/call/Opus bytes. Integers are little-endian.
// Association identity prevents a datagram from a previous binding surviving endpoint reuse;
// it is not a per-packet cryptographic authenticator. No media fragmentation or reliable retransmission.
class UdpVoiceProtocol final {
  public:
    static constexpr std::size_t kHeaderSize = PxUdpProtocol::kCommonHeaderSize + 16U;
    static constexpr std::size_t kMaxIdentityBytes = 128U;
    static constexpr std::size_t kMaxOpusBytes = 1275U;
    static constexpr std::size_t kMaxDatagramBytes = PxUdpProtocol::kDefaultMtu;

    static std::shared_ptr<Data> Build(std::string_view association, std::string_view call_id, std::uint32_t sequence, std::uint64_t capture_time_ms,
                                       std::span<const std::uint8_t> opus) {
        if (!ValidIdentity(association) || !ValidIdentity(call_id) || opus.empty() || opus.size() > kMaxOpusBytes) {
            return {};
        }
        const auto size = kHeaderSize + association.size() + call_id.size() + opus.size();
        if (size > kMaxDatagramBytes) {
            return {};
        }
        const auto data = Data::Allocate(size);
        auto bytes = data->MutableBytes();
        PxUdpProtocol::WriteCommon(bytes, PxUdpProtocol::kPktVoice);
        const auto header = bytes.subspan(PxUdpProtocol::kCommonHeaderSize, 16U);
        header[0] = static_cast<char>(association.size());
        header[1] = static_cast<char>(call_id.size());
        PxUdpProtocol::W16(header, 2, static_cast<std::uint16_t>(opus.size()));
        PxUdpProtocol::W32(header, 4, sequence);
        PxUdpProtocol::W32(header, 8, static_cast<std::uint32_t>(capture_time_ms));
        PxUdpProtocol::W32(header, 12, static_cast<std::uint32_t>(capture_time_ms >> 32U));
        auto position = bytes.begin() + kHeaderSize;
        position = std::ranges::copy(association, position).out;
        position = std::ranges::copy(call_id, position).out;
        std::ranges::transform(opus, position, [](std::uint8_t value) { return static_cast<char>(value); });
        return data;
    }

    static std::optional<UdpVoiceFrame> Parse(std::span<const char> bytes) {
        if (bytes.size() < kHeaderSize || bytes.size() > kMaxDatagramBytes || PxUdpProtocol::ParseCommon(bytes) != PxUdpProtocol::kPktVoice) {
            return {};
        }
        const auto header = bytes.subspan(PxUdpProtocol::kCommonHeaderSize, 16U);
        const auto association_size = static_cast<std::uint8_t>(header[0]);
        const auto call_size = static_cast<std::uint8_t>(header[1]);
        const auto opus_size = PxUdpProtocol::R16(header, 2);
        if (association_size == 0 || association_size > kMaxIdentityBytes || call_size == 0 || call_size > kMaxIdentityBytes || opus_size == 0 ||
            opus_size > kMaxOpusBytes || bytes.size() != kHeaderSize + association_size + call_size + opus_size) {
            return {};
        }
        const auto association = bytes.subspan(kHeaderSize, association_size);
        const auto call = bytes.subspan(kHeaderSize + association_size, call_size);
        const auto opus = bytes.subspan(kHeaderSize + association_size + call_size, opus_size);
        UdpVoiceFrame frame{
            .association_code = {association.begin(), association.end()},
            .call_id = {call.begin(), call.end()},
            .sequence = PxUdpProtocol::R32(header, 4),
            .capture_time_ms =
                static_cast<std::uint64_t>(PxUdpProtocol::R32(header, 8)) | (static_cast<std::uint64_t>(PxUdpProtocol::R32(header, 12)) << 32U),
            .opus = {opus.begin(), opus.end()},
        };
        if (!ValidIdentity(frame.association_code) || !ValidIdentity(frame.call_id)) {
            return {};
        }
        return frame;
    }

  private:
    static bool ValidIdentity(std::string_view value) {
        return !value.empty() && value.size() <= kMaxIdentityBytes && value.find('\0') == std::string_view::npos;
    }
};

} // namespace px
