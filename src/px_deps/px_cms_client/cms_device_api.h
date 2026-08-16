//
// Created by RGAA on 26/03/2025.
//

#ifndef PX_CMS_MANAGER_H
#define PX_CMS_MANAGER_H

#include <string>
#include <vector>
#include <tuple>
#include <memory>

#include "cms_server_info.h"
#include "px_common_new/expected.h"
#include "cms_errors.h"

namespace px_cms
{
    // class
    class CmsDevice;

    //
    using CmsDevicePtr = std::shared_ptr<CmsDevice>;

    class CmsDeviceApi {
    public:
        // ping
        static
        px::Result<bool, CmsApiError>
        Ping(const std::string& host,
             int port,
             const std::string& appkey);

        // create new device
        static
        px::Result<CmsDevicePtr, CmsApiError>
        RequestNewDevice(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& default_name,
                         const std::string& info);

        // update random password
        static
        px::Result<CmsDevicePtr, CmsApiError>
        UpdateRandomPwd(const std::string& host,
                        int port,
                        const std::string& appkey,
                        const std::string& device_id);

        // update safety password
        static
        px::Result<CmsDevicePtr, CmsApiError>
        UpdateSafetyPwd(const std::string& host,
                        int port,
                        const std::string& appkey,
                        const std::string& device_id,
                        const std::string& safety_pwd_md5);

        // get device
        static
        px::Result<CmsDevicePtr, CmsApiError>
        QueryDevice(const std::string& host,
                    int port,
                    const std::string& appkey,
                    const std::string& device_id);

        // whether the device is online right now
        // (it holds a live panel websocket connection to the CMS server)
        static
        bool
        IsDeviceOnline(const std::string& host,
                       int port,
                       const std::string& appkey,
                       const std::string& device_id);

        // update desktop link
        static
        px::Result<CmsDevicePtr, CmsApiError>
        UpdateDesktopLink(const std::string& host,
                          int port,
                          const std::string& appkey,
                          const std::string& device_id,
                          const std::string& desktop_link,
                          const std::string& desktop_link_raw);


        // update device name
        static
        px::Result<CmsDevicePtr, CmsApiError>
        UpdateDeviceName(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& device_id,
                         const std::string& device_name);

        // update device name
        static
        px::Result<CmsDevicePtr, CmsApiError>
        UpdateUsedTime(const std::string& host,
                         int port,
                         const std::string& appkey,
                         const std::string& device_id,
                         int period);
    };

}

#endif //PX_CMS_MANAGER_H
