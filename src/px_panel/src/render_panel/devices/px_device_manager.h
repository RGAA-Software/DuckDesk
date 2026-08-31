//
// Created by RGAA on 27/11/2025.
//

#ifndef GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H
#define GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H

#include <atomic>
#include <functional>
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
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        RequestNewDevice(
            const std::string& def_device_name,
            const std::string& info,
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

        // query device
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        QueryDevice(
            const std::string& device_id,
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        UpdateRandomPassword(
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

        // update desktop link to device
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        UpdateDesktopLink(
            const std::string& desktop_link,
            const std::string& desktop_link_raw,
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

        // update device name
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        UpdateDeviceName(
            const std::string& device_name,
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

        // append used time
        Result<std::shared_ptr<px_console::ConsoleDevice>, px_console::ConsoleApiError>
        UpdateUsedTime(
            int period,
            const std::shared_ptr<std::atomic_bool>& cancellation = nullptr);

    private:
        std::reference_wrapper<PxSettings> settings_;
        std::shared_ptr<PxContext> context_ = nullptr;

    };

}

#endif //GAMMARAYPREMIUM_GR_DEVICE_MANAGER_H
