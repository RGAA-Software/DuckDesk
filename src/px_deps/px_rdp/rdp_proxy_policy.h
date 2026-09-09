#pragma once

#include <span>
#include <cstdint>
#include <string_view>

namespace px::rdp {

[[nodiscard]] bool IsWorkspacePeer(std::string_view expected_user, std::string_view expected_domain, std::string_view user, std::string_view domain);
[[nodiscard]] bool IsAllowedStaticChannel(std::string_view name);
[[nodiscard]] bool IsAllowedDynamicChannel(std::string_view name);
enum class DeviceChannelDirection { kClientToServer, kServerToClient };
// Windows audio requires the rdpdr channel. Permit only its bounded handshake
// and empty device lists, never device announcements or device I/O.
[[nodiscard]] bool IsAudioDeviceHandshake(DeviceChannelDirection direction, std::span<const unsigned char> bytes, std::uint32_t flags,
                                          std::size_t total_size);
#ifdef _WIN32
// Exact deployment-pinned leaf certificate, valid now. The pin comes from the
// Service's local RDS certificate binding, never from an incoming RDP message.
[[nodiscard]] bool VerifyPinnedCertificate(std::span<const unsigned char> pem, std::string_view sha256_hex);
#endif

} // namespace px::rdp
