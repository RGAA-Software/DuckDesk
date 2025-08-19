//
// Created by RGAA on 19/08/2025.
//

#include "appkey_filter.h"

void AppKeyFilter::doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) {
    drogon::async_run([req, cb = std::move(cb), ccb = std::move(ccb)]() -> drogon::Task<> {
        auto userid = req->getOptionalParameter<std::string>("app_key");
        LOG_INFO << "*** has appkey: " << userid.has_value();

        ccb();
        co_return;
    });
}