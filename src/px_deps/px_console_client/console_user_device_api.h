//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_API_H
#define GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common_new/expected.h"
#include "console_errors.h"

namespace px_console
{

    class ConsoleUserDevice;

    struct ConsoleConnectionTicket {
        std::string ticket;
        // Rotating capability used to renew the same logical session. It is
        // transient launch state and must never be persisted by callers.
        std::string renewal_token;
        std::string launch_url;
        int64_t expires_at = 0;
        std::string logical_session_id;
        std::string stream_id;
        std::string join_mode;
        std::vector<std::string> permissions;
        // Serialized RtcSessionIceConfig. Kept in memory, never in a URL.
        std::string rtc_ice_config_json;
        std::string relay_host;
        int relay_port = 0;
        // Exact Relay/standard-RTC registration identity. Application
        // instances use an instance-scoped identity while ticket redemption
        // continues to use the base device ID from launch_url.
        std::string signal_device_id;
    };

    class ConsoleUserDeviceApi {
    public:
        // query user-devices
        static
        px::Result<std::vector<std::shared_ptr<ConsoleUserDevice>>, ConsoleApiError>
        QueryUserBindDevices(const std::string& host,
                             int port,
                             const std::string& access_token);

        // Issue a short-lived, one-time device connection ticket.
        static
        px::Result<ConsoleConnectionTicket, ConsoleApiError>
        IssueDeviceTicket(const std::string& host,
                          int port,
                          const std::string& access_token,
                          const std::string& device_id,
                          const std::string& client_nonce,
                          const std::vector<std::string>& requested_permissions);

        // Renew a previously issued ticket without changing its logical
        // session or stream identity. No user bearer token is required: the
        // rotating renewal capability is the authorization boundary.
        static
        px::Result<ConsoleConnectionTicket, ConsoleApiError>
        RenewConnectionTicket(const std::string& host,
                              int port,
                              const std::string& renewal_token,
                              const std::string& client_nonce);
    };

}
#endif //GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_API_H
