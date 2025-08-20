//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_CORS_MIDDLE_H
#define GAMMARAYPREMIUM_GR_CORS_MIDDLE_H

#include <drogon/HttpMiddleware.h>

using namespace drogon;

namespace tc
{

    class GrCorsMiddleware : public HttpMiddleware<GrCorsMiddleware> {
    public:
        GrCorsMiddleware() {
            LOG_INFO << "CorsMiddleware";
        }

        void invoke(const HttpRequestPtr &req, MiddlewareNextCallback &&nextCb, MiddlewareCallback &&mcb) override;
    };

}

#endif //GAMMARAYPREMIUM_GR_CORS_MIDDLE_H
