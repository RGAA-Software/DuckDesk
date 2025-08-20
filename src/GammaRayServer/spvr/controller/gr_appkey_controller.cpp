//
// Created by RGAA on 19/08/2025.
//

#include "gr_appkey_controller.h"
#include "spvr/gr_spvr_defs.h"
#include "spvr/gr_spvr_context.h"
#include "spvr/db/gr_device.h"
#include "spvr/db/gr_spvr_database.h"
#include "svr_base/gr_json_parser.h"
#include "svr_base/gr_http_response.h"
#include "spvr/auth/gr_spvr_auth_manager.h"

using namespace tc;

namespace api
{
    namespace v1
    {

        drogon::Task<drogon::HttpResponsePtr> GrAppkeyController::GetAppkey(drogon::HttpRequestPtr req) const {
            auto body = std::string(req->body());
            LOG_INFO << "body: " << body;
            auto r = tc::ParseJsonFromString(body);
            if (!r.has_value()) {
                co_return tc::MakeErrParamResp();
            }

            Json::Value ret;
            ret["appkey"] = grSpvrContext->GetAuthManager()->GetAppkey();
            co_return MakeOkResp(ret);
        }
    }
}