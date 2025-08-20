//
// Created by RGAA on 19/08/2025.
//

#include "gr_cors_middle.h"

namespace tc
{

    void GrCorsMiddleware::invoke(const HttpRequestPtr &req, MiddlewareNextCallback &&nextCb, MiddlewareCallback &&mcb) {
        LOG_INFO << "Middleware invoke.";
        std::string origin = req->getHeader("origin");
        if (origin.find("www.some-evil-place.com") != std::string::npos) {
            // intercept directly
            mcb(HttpResponse::newNotFoundResponse(req));
            return;
        }
        // Do something before calling the next middleware
        nextCb([mcb = std::move(mcb), origin](const HttpResponsePtr &resp) {
            // Do something after the next middleware returns
            resp->addHeader("Access-Control-Allow-Origin", origin);
            resp->addHeader("Access-Control-Allow-Credentials","true");
            mcb(resp);
        });
    }

}