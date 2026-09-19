#ifndef PIXELSPREMIUM_CONSOLE_USER_APP_API_H
#define PIXELSPREMIUM_CONSOLE_USER_APP_API_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "console_errors.h"
#include "console_resource_api.h"
#include "px_common/expected.h"
#include "px_common/secret_buffer.h"

namespace px_console {

struct ConsoleUserAppInstance {
    std::string instance_id;
    std::string app_id;
    std::string state;
    std::string error_code;
    bool reconnectable = false;
    std::int64_t revision{};
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

struct ConsoleNativeApplicationConnection final {
    std::string host{};
    int port{};
    std::string device_id{};
    std::string instance_id{};
    std::string app_type{};
    std::string session_id{};
    std::int64_t session_revision{};
    std::shared_ptr<const px::SecretBuffer> frontend_token{};
    std::string relay_host{};
    int relay_port{};
    std::string relay_admission_ticket{};
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

    static px::Result<ConsoleNativeApplicationConnection, ConsoleApiError> QueryNativeConnection(const std::string& host, int port,
                                                                                                 const std::string& access_token,
                                                                                                 const std::string& instance_id, bool view_only,
                                                                                                 const std::string& request_id, bool guest = false);

    static px::Result<ConsoleUserAppInstance, ConsoleApiError> StopInstance(const std::string& host, int port, const std::string& access_token,
                                                                            const std::string& instance_id, bool guest = false);
};

}  // namespace px_console
#endif
