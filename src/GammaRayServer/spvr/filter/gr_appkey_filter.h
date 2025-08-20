//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_APPKEY_FILTER_H
#define GAMMARAYPREMIUM_GR_APPKEY_FILTER_H

#include <drogon/HttpFilter.h>
using namespace drogon;

namespace tc
{

    class GrAppKeyFilter : public drogon::HttpFilter<GrAppKeyFilter> {
    public:
        GrAppKeyFilter() {
            LOG_DEBUG << "AppKeyFilter constructor";
        }

        void doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) override;
    };

}

#endif //GAMMARAYPREMIUM_GR_APPKEY_FILTER_H
