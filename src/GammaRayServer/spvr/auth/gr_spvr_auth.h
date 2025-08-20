//
// Created by RGAA on 20/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_SPVR_AUTH_H
#define GAMMARAYPREMIUM_GR_SPVR_AUTH_H

#include <string>

namespace tc
{
    class GrSpvrAuth {
    public:

        // max
        uint32_t max_online_clients_ = 0;

        // appkey for every client
        std::string appkey_;
    };
}

#endif //GAMMARAY// PREMIUM_GR_SPVR_AUTH_H
