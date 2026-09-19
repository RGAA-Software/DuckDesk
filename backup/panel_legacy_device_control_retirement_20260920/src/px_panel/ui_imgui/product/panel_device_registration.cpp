#include "panel_device_registration.h"

#include "panel_device_name.h"
#include "panel_product_runtime.h"

#include "px_common/md5.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_device_api.h"

namespace px::panel::product {

bool EnsurePanelDeviceRegistration(const std::shared_ptr<PanelProductRuntime>& runtime) {
    const auto endpoint = runtime->Config()->Console();
    if (!endpoint)
        return false;

    auto identity = runtime->Config()->Identity();
    const std::string desiredName{BuildDefaultDeviceName()};
    const bool customName{runtime->Config()->DeviceNameIsCustom() && !identity.deviceName.empty()};
    const std::string registrationName{customName ? identity.deviceName : desiredName};
    px_console::ConsoleDevicePtr existing{};
    if (!identity.deviceId.empty()) {
        const auto query = px_console::ConsoleDeviceApi::QueryDevice(endpoint->host, endpoint->port, endpoint->appKey, identity.deviceId);
        if (query)
            existing = query.value();
    }
    if (!existing) {
        const auto created = px_console::ConsoleDeviceApi::RequestNewDevice(endpoint->host, endpoint->port, endpoint->appKey, registrationName, "");
        if (!created || !created.value())
            return false;
        identity.deviceId = created.value()->device_id_;
        identity.deviceName = created.value()->device_name_;
        identity.randomPassword = created.value()->gen_random_pwd_;
        if (!runtime->Config()->SaveIdentity(identity))
            return false;
        static_cast<void>(runtime->Service()->RestartRender());
        return true;
    }

    const std::string persistedName{identity.deviceName.empty() ? existing->device_name_ : identity.deviceName};
    const std::string targetName{customName ? persistedName : desiredName};
    if (!customName && !identity.deviceName.empty() && !IsManagedDeviceName(identity.deviceName))
        return true;
    if (existing->device_name_ == targetName) {
        if (identity.deviceName == targetName)
            return true;
        identity.deviceName = targetName;
        return runtime->Config()->SaveIdentity(identity);
    }
    const auto updated = px_console::ConsoleDeviceApi::UpdateDeviceName(endpoint->host, endpoint->port, endpoint->appKey, identity.deviceId,
                                                                        targetName, MD5::Hex(identity.randomPassword));
    if (!updated || !updated.value())
        return false;
    identity.deviceName = targetName;
    if (!runtime->Config()->SaveIdentity(identity))
        return false;
    static_cast<void>(runtime->Service()->RestartRender());
    return true;
}

} // namespace px::panel::product
