//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_APP_KEY_CONTROLLER_H
#define GAMMARAYPREMIUM_APP_KEY_CONTROLLER_H

#include <drogon/HttpController.h>
using namespace drogon;

namespace api
{
    namespace v1
    {

        class GrAppkeyController : public drogon::HttpController<GrAppkeyController> {
        public:
            METHOD_LIST_BEGIN

            //path is /api/v1/GrAppkeyController/GetAppkey
            METHOD_ADD(GrAppkeyController::GetAppkey, "/GetAppkey", Get, "tc::GrTimeFilter", "tc::GrCorsMiddleware");

            METHOD_LIST_END

            // Impl
            drogon::Task<drogon::HttpResponsePtr> GetAppkey(HttpRequestPtr req) const;

        public:
            GrAppkeyController() {
                LOG_DEBUG << "User constructor!";
            }
        };

    } // namespace v1

} // namespace api


#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTROLLER_H
