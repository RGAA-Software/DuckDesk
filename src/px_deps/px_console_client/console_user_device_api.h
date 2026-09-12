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

#include "px_common/expected.h"
#include "console_errors.h"

namespace px_console
{

    class ConsoleUserDevice;

    struct ConsoleNativeDeviceConnection final {
        std::string host{};
        int port{};
        std::string device_id{};
        std::string password_hash{};
        std::string signal_device_id{};
        std::string relay_host{};
        int relay_port{};
    };

    class ConsoleUserDeviceApi {
    public:
        // query user-devices
        static
        px::Result<std::vector<std::shared_ptr<ConsoleUserDevice>>, ConsoleApiError>
        QueryUserBindDevices(const std::string& host,
                             int port,
                             const std::string& access_token);

        static px::Result<ConsoleNativeDeviceConnection, ConsoleApiError>
        QueryNativeConnection(const std::string& host,
                              int port,
                              const std::string& access_token,
                              const std::string& device_id);

    };

}
#endif //GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_API_H
