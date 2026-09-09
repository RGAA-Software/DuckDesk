#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "px_common/data.h"

namespace px::rdp {

inline constexpr std::size_t kMaxPayloadBytes{32 * 1024};
inline constexpr std::size_t kMaxWireBytes{kMaxPayloadBytes + 512};

// Issued by the authorized connection owner, never accepted as authorization itself.
// A reconnect must use a fresh binding; neither old buffered bytes nor NLA state resumes.
struct StreamBinding final {
    std::string connection_id{};
    std::uint64_t generation{0};
    [[nodiscard]] bool IsValid() const noexcept;
};

enum class PacketStatus { kData, kClose, kOtherMessage, kStaleBinding, kInvalid };

struct DecodedPacket final {
    PacketStatus status{PacketStatus::kInvalid};
    std::shared_ptr<const Data> payload{};
};

[[nodiscard]] std::shared_ptr<Data> EncodeData(const StreamBinding& binding, std::span<const char> bytes);
[[nodiscard]] std::shared_ptr<Data> EncodeClose(const StreamBinding& binding);
[[nodiscard]] std::shared_ptr<Data> EncodeOpen(const StreamBinding& binding);
// Accepted once, only from the already-authorized WebSocket, before constructing a byte stream.
[[nodiscard]] std::optional<StreamBinding> DecodeOpen(std::span<const char> wire);
[[nodiscard]] DecodedPacket DecodePacket(const StreamBinding& binding, std::span<const char> wire);

} // namespace px::rdp
