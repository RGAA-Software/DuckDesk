//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_TIME_FILTER_H
#define GAMMARAYPREMIUM_GR_TIME_FILTER_H

#pragma once

#include <drogon/HttpFilter.h>
using namespace drogon;

namespace tc
{
    class GrTimeFilter : public drogon::HttpFilter<GrTimeFilter> {
    public:
        GrTimeFilter() {
            LOG_DEBUG << "TimeFilter constructor";
        }

        void doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) override;
    };
}

#endif //GAMMARAYPREMIUM_GR_TIME_FILTER_H
