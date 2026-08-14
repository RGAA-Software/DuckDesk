//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_USER_DEVICE_API_H
#define GAMMARAYPREMIUM_SPVR_USER_DEVICE_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "tc_common_new/expected.h"
#include "spvr_errors.h"

namespace spvr
{

    class SpvrUserDevice;

    class SpvrUserDeviceApi {
    public:
        // query user-devices
        static
        tc::Result<std::vector<std::shared_ptr<SpvrUserDevice>>, SpvrApiError>
        QueryUserBindDevices(const std::string& host,
                             int port,
                             const std::string& appkey,
                             const std::string& uid,
                             int page,
                             int page_size);

        // add a device to user
        static
        tc::Result<std::shared_ptr<SpvrUserDevice>, SpvrApiError>
        AddDeviceForUser(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& uid,
                         const std::string& device_id);

        // remove a device from user
        static
        tc::Result<std::shared_ptr<SpvrUserDevice>, SpvrApiError>
        RemoveDeviceFromUser(const std::string& host,
                             int port,
                             const std::string& appkey,
                             const std::string& uid,
                             const std::string& device_id);
    };

}
#endif //GAMMARAYPREMIUM_SPVR_USER_DEVICE_API_H
