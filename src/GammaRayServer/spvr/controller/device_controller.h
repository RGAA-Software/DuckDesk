//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
#define GAMMARAYPREMIUM_DEVICE_CONTROLLER_H

#include <drogon/HttpController.h>
using namespace drogon;

namespace api
{
    namespace v1
    {
        class DeviceController : public drogon::HttpController<DeviceController> {
        public:
            METHOD_LIST_BEGIN

                //path is /api/v1/DeviceController/GetDeviceInfo/{arg1}
                METHOD_ADD(DeviceController::GetDeviceInfo, "/GetDeviceInfo/{device_id}", Get, "TimeFilter", "AppKeyFilter", "CorsMiddleware");
                //path is /api/v1/DeviceController/GetAllDevicesInfo
                METHOD_ADD(DeviceController::GetAllDevicesInfo, "/GetAllDevicesInfo", Get);

            METHOD_LIST_END

            drogon::Task<drogon::HttpResponsePtr> GetDeviceInfo(HttpRequestPtr req, std::string device_id) const;
            drogon::Task<drogon::HttpResponsePtr> GetAllDevicesInfo(HttpRequestPtr req) const;

        public:
            DeviceController() {
                LOG_DEBUG << "User constructor!";
            }
        };
    } // namespace v1

} // namespace api


#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
