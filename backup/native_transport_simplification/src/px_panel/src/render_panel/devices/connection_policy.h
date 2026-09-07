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

    enum class ConnectionMode {
        kAuto,
        kRelay,
        kDirect,
        kRtc,
        kUdpDirect,
        kInvalid,
    };

    enum class SelectedTransport {
        kUnavailable,
        kRelay,
        kWebSocket,
        kWebRtcStandard,
        kWebRtcDirect,
        kUdpDirect,
    };

    [[nodiscard]] inline ConnectionMode ResolveConnectionMode(
        bool force_relay, bool force_direct, bool force_rtc, bool force_udp) {
        const int selected_count = static_cast<int>(force_relay)
            + static_cast<int>(force_direct)
            + static_cast<int>(force_rtc)
            + static_cast<int>(force_udp);
        if (selected_count == 0) return ConnectionMode::kAuto;
        if (selected_count > 1) return ConnectionMode::kInvalid;
        if (force_relay) return ConnectionMode::kRelay;
        if (force_direct) return ConnectionMode::kDirect;
        if (force_rtc) return ConnectionMode::kRtc;
        return ConnectionMode::kUdpDirect;
    }

    inline bool NormalizeConnectionMode(
        bool& force_relay, bool& force_direct, bool& force_rtc, bool& force_udp) {
        if (ResolveConnectionMode(force_relay, force_direct, force_rtc, force_udp)
            != ConnectionMode::kInvalid) {
            return false;
        }
        // Old versions allowed RTC and UDP to remain checked together. There
        // is no reliable way to know which was selected last, so return the
        // ambiguous row to automatic selection instead of guessing.
        force_relay = false;
        force_direct = false;
        force_rtc = false;
        force_udp = false;
        return true;
    }

    [[nodiscard]] inline SelectedTransport SelectTransport(
        ConnectionMode mode, bool uses_console_ticket,
        bool direct_available, bool relay_available) {
        switch (mode) {
        case ConnectionMode::kRelay:
            return relay_available ? SelectedTransport::kRelay
                                   : SelectedTransport::kUnavailable;
        case ConnectionMode::kDirect:
            if (!direct_available) return SelectedTransport::kUnavailable;
            // A Console ticket can authorize the reliable WebSocket data
            // plane directly. Password-only IP connections must use Direct
            // RTC, whose initial HTTP handshake exchanges the password for a
            // short-lived Render grant before any data channel is opened.
            return uses_console_ticket ? SelectedTransport::kWebSocket
                                       : SelectedTransport::kWebRtcDirect;
        case ConnectionMode::kRtc:
            if (direct_available) return SelectedTransport::kWebRtcDirect;
            return relay_available ? SelectedTransport::kWebRtcStandard
                                   : SelectedTransport::kUnavailable;
        case ConnectionMode::kUdpDirect:
            if (!direct_available) return SelectedTransport::kUnavailable;
            // UDP carries media only and depends on an authenticated WS
            // control association. Until password-direct WS grants exist,
            // no-Console launches use the authenticated Direct RTC path.
            return uses_console_ticket ? SelectedTransport::kUdpDirect
                                       : SelectedTransport::kWebRtcDirect;
        case ConnectionMode::kAuto:
            if (direct_available) {
                return SelectedTransport::kWebRtcDirect;
            }
            return relay_available ? SelectedTransport::kWebRtcStandard
                                   : SelectedTransport::kUnavailable;
        case ConnectionMode::kInvalid:
            return SelectedTransport::kUnavailable;
        }
        return SelectedTransport::kUnavailable;
    }

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
