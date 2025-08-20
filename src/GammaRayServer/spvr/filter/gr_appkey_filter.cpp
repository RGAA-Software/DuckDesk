//
// Created by RGAA on 19/08/2025.
//

#include "gr_appkey_filter.h"
#include "svr_base/gr_http_response.h"
#include "spvr/gr_spvr_context.h"
#include "spvr/auth/gr_spvr_auth_manager.h"

namespace tc
{

    void GrAppKeyFilter::doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) {
        drogon::async_run([req, cb = std::move(cb), ccb = std::move(ccb)]() -> drogon::Task<> {
            auto opt_appkey = req->getOptionalParameter<std::string>("appkey");
            if (!opt_appkey.has_value()) {
                cb(MakeErAppkeyResp());
                co_return;
            }

            const auto& appkey = opt_appkey.value();
            if (appkey.empty()) {
                cb(MakeErAppkeyResp());
                co_return;
            }

            auto auth_mgr = grSpvrContext->GetAuthManager();
            if (auth_mgr->GetAppkey() != appkey) {
                cb(MakeErAppkeyResp());
                co_return;
            }

            ccb();
            co_return;
        });
    }

}