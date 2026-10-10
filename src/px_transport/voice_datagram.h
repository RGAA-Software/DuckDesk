#pragma once

#include "px_message.pb.h"
#include "transport.h"

namespace px::transport {

// Voice keeps its existing call ID, sequence and Opus jitter policy. It never queues on a reliable stream.
[[nodiscard]] Bytes EncodeVoiceDatagram(const Message& message);
[[nodiscard]] std::shared_ptr<Message> DecodeVoiceDatagram(std::span<const std::uint8_t> payload);

}  // namespace px::transport
