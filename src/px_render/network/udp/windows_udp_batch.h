#pragma once

#include <asio2/external/asio.hpp>
#include <mswsock.h>
#include <array>
#include <cstring>
#include "media_transport/media_wire.h"

namespace px {
enum class UdpBatchResult { kSent, kFallback, kIncomplete };

// Sunshine 3cba9bae, platform/windows/misc.cpp: per-message UDP segmentation, then unbatched fallback.
// Project-maintained GPL-3.0 adaptation; upstream source/license inventory: docs/native_udp_fec_upstream_implementation_plan.md.
// The socket and endpoint are borrowed synchronously. No socket option, worker or overlapped lifetime is introduced.
inline UdpBatchResult TryWindowsUdpBatch(asio::ip::udp::socket& socket, asio::ip::udp::endpoint endpoint, std::span<const media::Packet> packets) {
    if (packets.size() < 2 || packets.front().empty() || packets.front().size() > 65536 / packets.size())
        return UdpBatchResult::kFallback;
    media::Packet payload{};
    payload.reserve(packets.size() * packets.front().size());
    for (const auto& packet : packets) {
        if (packet.size() != packets.front().size())
            return UdpBatchResult::kFallback;
        payload.insert(payload.end(), packet.begin(), packet.end());
    }
    // Winsock ABI descriptors are transient, borrowed only for this synchronous call; payload owns the bytes.
    WSABUF buffer{};
    buffer.buf = reinterpret_cast<char*>(payload.data());
    buffer.len = static_cast<ULONG>(payload.size());
    alignas(WSACMSGHDR) std::array<char, WSA_CMSG_SPACE(sizeof(DWORD))> control{};
    WSAMSG message{};
    message.name = endpoint.data();
    message.namelen = static_cast<int>(endpoint.size());
    message.lpBuffers = &buffer;
    message.dwBufferCount = 1;
    message.Control.buf = control.data();
    message.Control.len = static_cast<ULONG>(control.size());
    auto& header = *WSA_CMSG_FIRSTHDR(&message);
    header.cmsg_level = IPPROTO_UDP;
    header.cmsg_type = UDP_SEND_MSG_SIZE;
    header.cmsg_len = WSA_CMSG_LEN(sizeof(DWORD));
    const auto segment_size = static_cast<DWORD>(packets.front().size());
    std::memcpy(WSA_CMSG_DATA(&header), &segment_size, sizeof(segment_size));
    DWORD bytes{};
    if (WSASendMsg(socket.native_handle(), &message, 0, &bytes, nullptr, nullptr) == SOCKET_ERROR)
        return UdpBatchResult::kFallback;
    // Never resend an ambiguously successful short batch: doing so could duplicate already accepted datagrams.
    return bytes == payload.size() ? UdpBatchResult::kSent : UdpBatchResult::kIncomplete;
}
} // namespace px
