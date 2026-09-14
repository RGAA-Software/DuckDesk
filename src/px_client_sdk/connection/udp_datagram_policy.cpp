#include "udp_datagram_policy.h"

#include <asio/ip/address.hpp>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <ws2ipdef.h>
#endif

namespace px {
namespace {

[[nodiscard]] bool IsPrivateOrLinkLocal(const asio::ip::address& address) {
    if (address.is_loopback())
        return true;
    if (address.is_v4()) {
        const auto bytes = address.to_v4().to_bytes();
        return bytes[0] == 10 || (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) || (bytes[0] == 192 && bytes[1] == 168) ||
               (bytes[0] == 169 && bytes[1] == 254);
    }
    if (!address.is_v6())
        return false;
    const auto bytes = address.to_v6().to_bytes();
    return address.to_v6().is_link_local() || (bytes[0] & 0xFEU) == 0xFCU;
}

#ifdef _WIN32
[[nodiscard]] SOCKADDR_INET ToWindowsAddress(const asio::ip::address& address) {
    SOCKADDR_INET result{};
    if (address.is_v4()) {
        result.Ipv4.sin_family = AF_INET;
        const auto bytes = address.to_v4().to_bytes();
        result.Ipv4.sin_addr.S_un.S_un_b.s_b1 = bytes[0];
        result.Ipv4.sin_addr.S_un.S_un_b.s_b2 = bytes[1];
        result.Ipv4.sin_addr.S_un.S_un_b.s_b3 = bytes[2];
        result.Ipv4.sin_addr.S_un.S_un_b.s_b4 = bytes[3];
    } else {
        result.Ipv6.sin6_family = AF_INET6;
        const auto bytes = address.to_v6().to_bytes();
        std::ranges::copy(bytes, result.Ipv6.sin6_addr.u.Byte);
        result.Ipv6.sin6_scope_id = address.to_v6().scope_id();
    }
    return result;
}

[[nodiscard]] bool IsUnspecifiedNextHop(const SOCKADDR_INET& next_hop) {
    if (next_hop.si_family == AF_INET)
        return next_hop.Ipv4.sin_addr.S_un.S_addr == INADDR_ANY;
    if (next_hop.si_family != AF_INET6)
        return false;
    return std::ranges::all_of(next_hop.Ipv6.sin6_addr.u.Byte, [](const std::uint8_t byte) { return byte == 0; });
}

[[nodiscard]] bool ContainsCaseInsensitive(const std::wstring_view text, const std::wstring_view token) {
    return std::search(text.begin(), text.end(), token.begin(), token.end(),
                       [](const wchar_t left, const wchar_t right) { return std::towlower(left) == std::towlower(right); }) != text.end();
}

[[nodiscard]] bool IsVpnInterface(const MIB_IF_ROW2& interface_row) {
    if (interface_row.Type == IF_TYPE_PPP || interface_row.Type == IF_TYPE_PROP_VIRTUAL || interface_row.Type == IF_TYPE_TUNNEL ||
        (interface_row.Mtu != 0 && interface_row.Mtu < 1500)) {
        return true;
    }
    const std::wstring_view alias{interface_row.Alias};
    const std::wstring_view description{interface_row.Description};
    return ContainsCaseInsensitive(alias, L"VPN") || ContainsCaseInsensitive(description, L"VPN") || ContainsCaseInsensitive(alias, L"ZeroTier") ||
           ContainsCaseInsensitive(description, L"ZeroTier") || ContainsCaseInsensitive(alias, L"WireGuard") ||
           ContainsCaseInsensitive(description, L"WireGuard") || ContainsCaseInsensitive(alias, L"OpenVPN") ||
           ContainsCaseInsensitive(description, L"OpenVPN");
}

[[nodiscard]] UdpDatagramProfile SelectWindowsProfile(const asio::ip::address& remote_address) {
    auto destination = ToWindowsAddress(remote_address);
    MIB_IPFORWARD_ROW2 route{};
    SOCKADDR_INET source{};
    const auto route_result = GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &route, &source);
    if (route_result != NO_ERROR) {
        const auto kind = remote_address.is_v4() ? UdpPathKind::kRemoteIpv4 : UdpPathKind::kRemoteIpv6;
        return {.kind = kind, .datagram_size = DatagramSizeForPath(kind)};
    }

    MIB_IF_ROW2 interface_row{};
    interface_row.InterfaceLuid = route.InterfaceLuid;
    if (GetIfEntry2(&interface_row) != NO_ERROR) {
        const auto kind = remote_address.is_v4() ? UdpPathKind::kRemoteIpv4 : UdpPathKind::kRemoteIpv6;
        return {.kind = kind, .datagram_size = DatagramSizeForPath(kind)};
    }
    if (IsVpnInterface(interface_row)) {
        return {.kind = UdpPathKind::kVpn, .datagram_size = kUdpVpnDatagramSize, .interface_mtu = interface_row.Mtu};
    }
    if (remote_address.is_loopback() || IsUnspecifiedNextHop(route.NextHop)) {
        return {.kind = UdpPathKind::kLan, .datagram_size = kUdpLanDatagramSize, .interface_mtu = interface_row.Mtu};
    }
    const auto kind = remote_address.is_v4() ? UdpPathKind::kRemoteIpv4 : UdpPathKind::kRemoteIpv6;
    return {.kind = kind, .datagram_size = DatagramSizeForPath(kind), .interface_mtu = interface_row.Mtu};
}
#endif

} // namespace

UdpDatagramProfile SelectUdpDatagramProfile(const asio::ip::address& remote_address) {
    if (remote_address.is_unspecified())
        return {};
#ifdef _WIN32
    return SelectWindowsProfile(remote_address);
#else
    const auto kind = IsPrivateOrLinkLocal(remote_address) ? UdpPathKind::kLan
                      : remote_address.is_v4()             ? UdpPathKind::kRemoteIpv4
                                                           : UdpPathKind::kRemoteIpv6;
    return {.kind = kind, .datagram_size = DatagramSizeForPath(kind)};
#endif
}

std::string UdpPathKindName(const UdpPathKind kind) {
    switch (kind) {
    case UdpPathKind::kLan:
        return "lan";
    case UdpPathKind::kVpn:
        return "vpn";
    case UdpPathKind::kRemoteIpv4:
        return "remote-ipv4";
    case UdpPathKind::kRemoteIpv6:
        return "remote-ipv6";
    case UdpPathKind::kUnknown:
    default:
        return "unknown";
    }
}

} // namespace px
