#pragma once

#include <Winsock2.h>
#include <Iphlpapi.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace px {

// The connection's real kernel-reported client PID, not the untrusted ?pid query.
// Ports are host-order; the server must have already restricted the peer to IPv4 loopback.
inline std::optional<DWORD> FindLoopbackTcpClientPid(std::uint16_t client_port, std::uint16_t server_port) {
    ULONG bytes{};
    if (GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) != ERROR_INSUFFICIENT_BUFFER) {
        return std::nullopt;
    }
    for (int attempt{}; attempt < 3; ++attempt) {
        if (bytes < sizeof(DWORD) || bytes > 32 * 1024 * 1024) {
            return std::nullopt;
        }
        std::vector<std::byte> storage(bytes);
        const auto status = GetExtendedTcpTable(storage.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        if (status == ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        if (status != NO_ERROR) {
            return std::nullopt;
        }
        DWORD count{};
        std::memcpy(&count, storage.data(), sizeof(count));
        constexpr auto rows_offset = offsetof(MIB_TCPTABLE_OWNER_PID, table);
        if (storage.size() < rows_offset || count > (storage.size() - rows_offset) / sizeof(MIB_TCPROW_OWNER_PID)) {
            return std::nullopt;
        }
        std::optional<DWORD> matched{};
        for (DWORD index{}; index < count; ++index) {
            MIB_TCPROW_OWNER_PID row{};
            std::memcpy(&row, storage.data() + rows_offset + static_cast<std::size_t>(index) * sizeof(row), sizeof(row));
            if (row.dwState == MIB_TCP_STATE_ESTAB && row.dwLocalAddr == htonl(INADDR_LOOPBACK) && row.dwRemoteAddr == htonl(INADDR_LOOPBACK) &&
                ntohs(static_cast<u_short>(row.dwLocalPort)) == client_port && ntohs(static_cast<u_short>(row.dwRemotePort)) == server_port) {
                if (matched && *matched != row.dwOwningPid) {
                    return std::nullopt;
                }
                matched = row.dwOwningPid;
            }
        }
        return matched;
    }
    return std::nullopt;
}
} // namespace px
