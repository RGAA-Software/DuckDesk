#ifndef PX_CONNECTION_POLICY_H
#define PX_CONNECTION_POLICY_H

#include <string_view>

namespace px::connection_policy
{
    inline constexpr std::string_view kConsoleDeviceTicket = "console_ticket";
    inline constexpr std::string_view kConsoleAppTicket = "console_app_ticket";
    inline constexpr std::string_view kExplicitDirect = "direct";
    inline constexpr std::string_view kSharedLinkDirect = "shared_link_direct";

    enum class LaunchPolicy {
        kConsoleTicket,
        kExplicitDirect,
        kReject,
    };

    [[nodiscard]] inline bool IsConsoleTicket(std::string_view connect_type) {
        return connect_type == kConsoleDeviceTicket || connect_type == kConsoleAppTicket;
    }

    [[nodiscard]] inline bool SharedLinkUsesConsoleTicket(
        std::string_view connect_type, bool is_logged_in) {
        return connect_type == kSharedLinkDirect && is_logged_in;
    }

    [[nodiscard]] inline LaunchPolicy Classify(
        std::string_view connect_type,
        std::string_view remote_device_id,
        std::string_view host,
        int port) {
        if (IsConsoleTicket(connect_type)) {
            return LaunchPolicy::kConsoleTicket;
        }
        if (connect_type == kExplicitDirect
            && remote_device_id.empty()
            && !host.empty()
            && port > 0) {
            return LaunchPolicy::kExplicitDirect;
        }
        // A link:// payload is an explicit password-bearing direct endpoint.
        // Keep the remote id for display/transport identity, but never use this
        // mode as an authorization-free device-id lookup or Relay fallback.
        if (connect_type == kSharedLinkDirect
            && !remote_device_id.empty()
            && !host.empty()
            && port > 0) {
            return LaunchPolicy::kExplicitDirect;
        }
        return LaunchPolicy::kReject;
    }

    [[nodiscard]] inline bool IsLegacyManagedConnection(
        std::string_view connect_type,
        std::string_view remote_device_id) {
        return !remote_device_id.empty()
            && !IsConsoleTicket(connect_type)
            && connect_type != kSharedLinkDirect;
    }

    [[nodiscard]] inline bool IsUnclassifiedDirectConnection(
        std::string_view connect_type,
        std::string_view remote_device_id,
        std::string_view host,
        int port) {
        return connect_type.empty() && remote_device_id.empty() && !host.empty() && port > 0;
    }
}

#endif // PX_CONNECTION_POLICY_H
