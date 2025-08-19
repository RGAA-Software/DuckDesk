//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_TIME_FILTER_H
#define GAMMARAYPREMIUM_TIME_FILTER_H

#pragma once

#include <drogon/HttpFilter.h>
using namespace drogon;

class TimeFilter : public drogon::HttpFilter<TimeFilter> {
public:
    TimeFilter() {
        LOG_DEBUG << "TimeFilter constructor";
    }

    void doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) override;
};


#endif //GAMMARAYPREMIUM_TIME_FILTER_H
