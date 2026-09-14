#pragma once

#include <cstdint>
#include <string>

namespace asio::ip {
class address;
}

namespace px {

enum class UdpPathKind : std::uint8_t {
    kUnknown,
    kLan,
    kVpn,
    kRemoteIpv4,
    kRemoteIpv6,
};

struct UdpDatagramProfile final {
    UdpPathKind kind{UdpPathKind::kUnknown};
    std::uint16_t datagram_size{1200};
    std::uint32_t interface_mtu{};
};

inline constexpr std::uint16_t kUdpUnknownDatagramSize{1200};
inline constexpr std::uint16_t kUdpLanDatagramSize{1400};
inline constexpr std::uint16_t kUdpVpnDatagramSize{1040};
inline constexpr std::uint16_t kUdpRemoteIpv4DatagramSize{1040};
inline constexpr std::uint16_t kUdpRemoteIpv6DatagramSize{1200};

[[nodiscard]] constexpr std::uint16_t DatagramSizeForPath(const UdpPathKind kind) {
    switch (kind) {
    case UdpPathKind::kLan:
        return kUdpLanDatagramSize;
    case UdpPathKind::kVpn:
        return kUdpVpnDatagramSize;
    case UdpPathKind::kRemoteIpv4:
        return kUdpRemoteIpv4DatagramSize;
    case UdpPathKind::kRemoteIpv6:
        return kUdpRemoteIpv6DatagramSize;
    case UdpPathKind::kUnknown:
    default:
        return kUdpUnknownDatagramSize;
    }
}

[[nodiscard]] UdpDatagramProfile SelectUdpDatagramProfile(const asio::ip::address& remote_address);
[[nodiscard]] std::string UdpPathKindName(UdpPathKind kind);

} // namespace px
