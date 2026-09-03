#pragma once

#include <string>

namespace px::render {

enum class TransportKind {
    kWebSocket,
    kUdp,
    kRelay,
    kWebRtc,
    kWebRtcLocal,
};

enum class TransportChannelKind {
    kControl,
    kMedia,
    kFileTransfer,
    kVoice,
};

struct TransportRoute final {
    TransportKind kind{TransportKind::kWebSocket};
    TransportChannelKind channel{TransportChannelKind::kControl};
    std::string transport_id;
    std::string logical_session_id;
    std::string connection_id;
    std::string stream_id;
};

}  // namespace px::render
