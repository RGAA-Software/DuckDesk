//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
#define GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H

#include <drogon/HttpController.h>
using namespace drogon;

namespace api
{
    namespace v1
    {
        class GrProfileController : public drogon::HttpController<GrProfileController> {
        public:
            METHOD_LIST_BEGIN

            // path is /api/v1/GrProfileController/AddProfile
            METHOD_ADD(GrProfileController::AddProfile, "/AddProfile", Post);

            //path is /api/v1/GrProfileController/GetDeviceInfo/{arg1}
            METHOD_ADD(GrProfileController::GetProfileInfo, "/GetProfileInfo/{profile_id}", Get);

            //path is /api/v1/GrProfileController/GetProfilesInfo
            METHOD_ADD(GrProfileController::GetProfilesInfo, "/GetProfilesInfo", Get);

            METHOD_LIST_END

            drogon::Task<drogon::HttpResponsePtr> AddProfile(HttpRequestPtr req) const;
            drogon::Task<drogon::HttpResponsePtr> GetProfileInfo(HttpRequestPtr req, std::string profile_id) const;
            drogon::Task<drogon::HttpResponsePtr> GetProfilesInfo(HttpRequestPtr req, int page, int page_size) const;

        public:
            GrProfileController() {
                LOG_DEBUG << "User constructor!";
            }
        };
    } // namespace v1

} // namespace api


#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
