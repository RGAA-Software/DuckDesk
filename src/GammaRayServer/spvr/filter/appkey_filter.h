//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_APPKEY_FILTER_H
#define GAMMARAYPREMIUM_APPKEY_FILTER_H

#include <drogon/HttpFilter.h>
using namespace drogon;

class AppKeyFilter : public drogon::HttpFilter<AppKeyFilter> {
public:
    AppKeyFilter() {
        LOG_DEBUG << "AppKeyFilter constructor";
    }

    void doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) override;
};

#endif //GAMMARAYPREMIUM_APPKEY_FILTER_H
