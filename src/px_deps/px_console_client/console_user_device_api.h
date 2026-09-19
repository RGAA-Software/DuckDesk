//
// Created by RGAA on 28/11/2025.
//

#ifndef PIXELSPREMIUM_CONSOLE_USER_DEVICE_API_H
#define PIXELSPREMIUM_CONSOLE_USER_DEVICE_API_H

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "console_errors.h"
#include "px_common/expected.h"
#include "px_common/secret_buffer.h"

namespace px_console {

class ConsoleUserDevice;

struct ConsoleNativeDeviceConnection final {
    std::string host{};
    int port{};
    std::string device_id{};
    std::string session_id{};
    std::int64_t session_revision{};
    std::shared_ptr<const px::SecretBuffer> frontend_token{};
    std::string relay_host{};
    int relay_port{};
    std::string relay_admission_ticket{};
};

class ConsoleUserDeviceApi {
public:
    // query user-devices
    static px::Result<std::vector<std::shared_ptr<ConsoleUserDevice>>, ConsoleApiError> QueryUserBindDevices(const std::string& host, int port,
                                                                                                             const std::string& access_token);

    static px::Result<ConsoleNativeDeviceConnection, ConsoleApiError> QueryNativeConnection(const std::string& host, int port,
                                                                                            const std::string& access_token,
                                                                                            const std::string& device_id, bool view_only = false);
};

}  // namespace px_console
#endif  // PIXELSPREMIUM_CONSOLE_USER_DEVICE_API_H
