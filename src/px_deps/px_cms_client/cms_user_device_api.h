//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H
#define GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H

#include <map>
#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "px_common_new/expected.h"
#include "cms_errors.h"

namespace px_cms
{

    class CmsUserDevice;

    class CmsUserDeviceApi {
    public:
        // query user-devices
        static
        px::Result<std::vector<std::shared_ptr<CmsUserDevice>>, CmsApiError>
        QueryUserBindDevices(const std::string& host,
                             int port,
                             const std::string& appkey,
                             const std::string& uid,
                             int page,
                             int page_size);

        // add a device to user
        static
        px::Result<std::shared_ptr<CmsUserDevice>, CmsApiError>
        AddDeviceForUser(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& uid,
                         const std::string& device_id);

        // remove a device from user
        static
        px::Result<std::shared_ptr<CmsUserDevice>, CmsApiError>
        RemoveDeviceFromUser(const std::string& host,
                             int port,
                             const std::string& appkey,
                             const std::string& uid,
                             const std::string& device_id);
    };

}
#endif //GAMMARAYPREMIUM_CMS_USER_DEVICE_API_H
