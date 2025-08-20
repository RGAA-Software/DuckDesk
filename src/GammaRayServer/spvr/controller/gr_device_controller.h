//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_DEVICE_CONTROLLER_H
#define GAMMARAYPREMIUM_DEVICE_CONTROLLER_H

#include <drogon/HttpController.h>
using namespace drogon;

namespace api
{
    namespace v1
    {
        class GrDeviceController : public drogon::HttpController<GrDeviceController> {
        public:
            METHOD_LIST_BEGIN
            //path is /api/v1/GrDeviceController/GetDeviceInfo/{arg1}
            METHOD_ADD(GrDeviceController::CreateDevice, "/CreateDevice", Post, "tc::GrTimeFilter", "tc::GrAppKeyFilter", "tc::GrCorsMiddleware");

            //path is /api/v1/GrDeviceController/GetDeviceInfo/{arg1}
            METHOD_ADD(GrDeviceController::GetDeviceInfo, "/GetDeviceInfo/{device_id}", Get, "tc::GrTimeFilter", "tc::GrAppKeyFilter", "tc::GrCorsMiddleware");

            //path is /api/v1/GrDeviceController/GetAllDevicesInfo
            METHOD_ADD(GrDeviceController::GetAllDevicesInfo, "/GetAllDevicesInfo", Get, "tc::GrTimeFilter", "tc::GrAppKeyFilter", "tc::GrCorsMiddleware");

            METHOD_LIST_END

            // Impl
            drogon::Task<drogon::HttpResponsePtr> CreateDevice(HttpRequestPtr req) const;
            drogon::Task<drogon::HttpResponsePtr> GetDeviceInfo(HttpRequestPtr req, std::string device_id) const;
            drogon::Task<drogon::HttpResponsePtr> GetAllDevicesInfo(HttpRequestPtr req) const;

        public:
            GrDeviceController() {
                LOG_DEBUG << "User constructor!";
            }
        };
    } // namespace v1

} // namespace api


#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
