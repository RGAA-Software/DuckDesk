//
// Created by RGAA on 26/03/2025.
//

#ifndef PX_CONSOLE_MANAGER_H
#define PX_CONSOLE_MANAGER_H

#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "console_server_info.h"
#include "px_common_new/expected.h"
#include "console_errors.h"

namespace px_console
{
    // class
    class ConsoleDevice;

    //
    using ConsoleDevicePtr = std::shared_ptr<ConsoleDevice>;

    class ConsoleDeviceApi {
    public:
        // ping
        static
        px::Result<bool, ConsoleApiError>
        Ping(const std::string& host,
             int port,
             const std::string& appkey);

        // create new device
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        RequestNewDevice(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& default_name,
                         const std::string& info);

        // update random password
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        UpdateRandomPwd(const std::string& host,
                        int port,
                        const std::string& appkey,
                        const std::string& device_id);

        // update safety password
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        UpdateSafetyPwd(const std::string& host,
                        int port,
                        const std::string& appkey,
                        const std::string& device_id,
                        const std::string& safety_pwd_md5);

        // get device
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        QueryDevice(const std::string& host,
                    int port,
                    const std::string& appkey,
                    const std::string& device_id);

        // whether the device is online right now
        // (it holds a live panel websocket connection to the Console server)
        static
        bool
        IsDeviceOnline(const std::string& host,
                       int port,
                       const std::string& appkey,
                       const std::string& device_id);

        // update desktop link
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        UpdateDesktopLink(const std::string& host,
                          int port,
                          const std::string& appkey,
                          const std::string& device_id,
                          const std::string& desktop_link,
                          const std::string& desktop_link_raw);


        // update device name
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        UpdateDeviceName(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& device_id,
                         const std::string& device_name);

        // update device name
        static
        px::Result<ConsoleDevicePtr, ConsoleApiError>
        UpdateUsedTime(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& device_id,
                         int period);
    };

}

#endif //PX_CONSOLE_MANAGER_H
