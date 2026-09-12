#ifndef GAMMARAYPREMIUM_CONSOLE_USER_APP_API_H
#define GAMMARAYPREMIUM_CONSOLE_USER_APP_API_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "console_errors.h"
#include "console_user_device_api.h"
#include "px_common/expected.h"
#include "px_common/secret_buffer.h"

namespace px_console {

struct ConsoleUserAppInstance {
    std::string instance_id;
    std::string app_id;
    std::string state;
    std::string error_code;
    bool reconnectable = false;
};

struct ConsoleUserApplication {
    std::string app_id;
    std::string app_type{};
    std::string name;
    std::string access_mode;
    std::string cover_url;
    int64_t version = 0;
    std::shared_ptr<ConsoleUserAppInstance> running_instance;
};

// Stable connection metadata for a native client. Authorization is the
// authenticated Console account/guest request plus the Render password; this
// object is not a bearer capability and is not redeemed or renewed.
struct ConsoleNativeApplicationConnection final {
    std::string host{};
    int port{};
    std::string device_id{};
    std::string instance_id{};
    std::string app_type{};
    std::string password_hash{};
    std::string signal_device_id{};
    std::string relay_host{};
    int relay_port{};
    std::shared_ptr<const px::SecretBuffer> rdp_configuration{};
};

class ConsoleUserAppApi {
  public:
    static px::Result<std::string, ConsoleApiError> CreateGuestSession(const std::string& host, int port, const std::string& client_nonce);

    static px::Result<std::vector<ConsoleUserApplication>, ConsoleApiError> QueryApps(const std::string& host, int port,
                                                                                      const std::string& access_token, bool guest = false);

    static px::Result<std::vector<ConsoleUserAppInstance>, ConsoleApiError> QueryInstances(const std::string& host, int port,
                                                                                           const std::string& access_token, bool guest = false);

    static px::Result<ConsoleUserAppInstance, ConsoleApiError> StartApp(const std::string& host, int port, const std::string& access_token,
                                                                        const std::string& app_id, const std::string& client_nonce,
                                                                        bool guest = false);

    static px::Result<ConsoleNativeApplicationConnection, ConsoleApiError>
    QueryNativeConnection(const std::string& host, int port, const std::string& access_token, const std::string& instance_id,
                          bool view_only, bool guest = false);

    static px::Result<ConsoleUserAppInstance, ConsoleApiError> StopInstance(const std::string& host, int port, const std::string& access_token,
                                                                            const std::string& instance_id, bool guest = false);
};

} // namespace px_console
#endif
