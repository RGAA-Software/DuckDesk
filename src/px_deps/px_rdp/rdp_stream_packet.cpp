#include "rdp_stream_packet.h"

#include "px_message.pb.h"

namespace px::rdp {
namespace {

std::shared_ptr<Data> Encode(const StreamBinding& binding, const RdpStreamPacket::Kind kind, const std::span<const char> bytes) {
    if (!binding.IsValid() || bytes.size() > kMaxPayloadBytes || (kind == RdpStreamPacket::DATA && bytes.empty())) {
        return {};
    }
    Message message{};
    message.set_type(kRdpStream);
    // Protobuf owns the mutable submessage; the borrowed ABI result is used only at this boundary.
    auto& packet = *message.mutable_rdp_stream();
    packet.set_version(1);
    packet.set_connection_id(binding.connection_id);
    packet.set_generation(binding.generation);
    packet.set_kind(kind);
    if (!bytes.empty()) {
        packet.set_payload(bytes.data(), bytes.size());
    }
    return Data::From(message.SerializeAsString());
}

} // namespace

bool StreamBinding::IsValid() const noexcept {
    return !connection_id.empty() && connection_id.size() <= 128 && generation != 0;
}

std::shared_ptr<Data> EncodeData(const StreamBinding& binding, const std::span<const char> bytes) {
    return Encode(binding, RdpStreamPacket::DATA, bytes);
}

std::shared_ptr<Data> EncodeClose(const StreamBinding& binding) {
    return Encode(binding, RdpStreamPacket::CLOSE, {});
}

std::shared_ptr<Data> EncodeOpen(const StreamBinding& binding) {
    return Encode(binding, RdpStreamPacket::OPEN, {});
}

DecodedPacket DecodePacket(const StreamBinding& binding, const std::span<const char> wire) {
    if (!binding.IsValid() || wire.empty() || wire.size() > kMaxWireBytes) {
        return {};
    }
    Message message{};
    if (!message.ParseFromArray(wire.data(), static_cast<int>(wire.size()))) {
        return {};
    }
    if (message.type() != kRdpStream) {
        return {.status = PacketStatus::kOtherMessage};
    }
    if (!message.has_rdp_stream()) {
        return {};
    }
    const auto& packet = message.rdp_stream();
    if (packet.version() != 1 || packet.payload().size() > kMaxPayloadBytes) {
        return {};
    }
    if (packet.connection_id() != binding.connection_id || packet.generation() != binding.generation) {
        return {.status = PacketStatus::kStaleBinding};
    }
    if (packet.kind() == RdpStreamPacket::CLOSE && packet.payload().empty()) {
        return {.status = PacketStatus::kClose};
    }
    if (packet.kind() != RdpStreamPacket::DATA || packet.payload().empty()) {
        return {};
    }
    return {.status = PacketStatus::kData, .payload = Data::From(packet.payload())};
}

std::optional<StreamBinding> DecodeOpen(const std::span<const char> wire) {
    if (wire.empty() || wire.size() > kMaxWireBytes) {
        return {};
    }
    Message message{};
    if (!message.ParseFromArray(wire.data(), static_cast<int>(wire.size())) || message.type() != kRdpStream || !message.has_rdp_stream()) {
        return {};
    }
    const auto& packet = message.rdp_stream();
    StreamBinding binding{packet.connection_id(), packet.generation()};
    if (packet.version() != 1 || packet.kind() != RdpStreamPacket::OPEN || !packet.payload().empty() || !binding.IsValid()) {
        return {};
    }
    return binding;
}

} // namespace px::rdp
