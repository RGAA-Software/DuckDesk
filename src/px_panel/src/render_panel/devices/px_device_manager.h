//
// Created by RGAA on 27/11/2025.
//

#ifndef GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H
#define GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H

#include <memory>
#include <string>

#include "px_common_new/expected.h"
#include "px_console_client/console_errors.h"

namespace px_console
{
    class ConsoleDevice;
}

namespace px
{

    class PxContext;
    class PxSettings;

    class PxDeviceManager {
    public:
        explicit PxDeviceManager(const std::shared_ptr<PxContext>& ctx);
        // request new device
        // def_device_name: D-{last segment of ip}
        // info: empty
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError> RequestNewDevice(const std::string& def_device_name, const std::string& info);

        // query device
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError> QueryDevice(const std::string& device_id);

        // update desktop link to device
        bool UpdateDesktopLink(const std::string& desktop_link, const std::string& desktop_link_raw);

        // update device name
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError> UpdateDeviceName(const std::string& device_name);

        // append used time
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError> UpdateUsedTime(int period);

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;

    };

}

#endif //GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H
