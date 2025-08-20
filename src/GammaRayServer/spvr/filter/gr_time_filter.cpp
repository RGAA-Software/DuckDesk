//
// Created by RGAA on 19/08/2025.
//

#include "gr_time_filter.h"

#include <drogon/utils/coroutine.h>
#include <drogon/drogon.h>
#include <string>
#include "svr_base/redis_cache.h"
#include "svr_base/date_funcs.h"
#define VDate "visitDate"

namespace tc
{

    void GrTimeFilter::doFilter(const HttpRequestPtr &req, FilterCallback &&cb, FilterChainCallback &&ccb) {
        drogon::async_run([req, cb = std::move(cb), ccb = std::move(ccb)]() -> drogon::Task<> {
            auto userid = req->getOptionalParameter<std::string>("userid");
            LOG_INFO << "userId has value: " << userid.has_value();

            ccb();
            co_return;
//        if (!userid.empty())
//        {
//            auto key = userid + "." + VDate;
//            const trantor::Date now = trantor::Date::date();
//            auto redisClient = drogon::app().getFastRedisClient();
//            try
//            {
//                auto lastDate =
//                co_await getFromCache<trantor::Date>(key, redisClient);
//                LOG_TRACE << "last:" << lastDate.toFormattedString(false);
//                co_await updateCache(key, now, redisClient);
//                if (now > lastDate.after(10))
//                {
//                    // 10 sec later can visit again;
//                    ccb();
//                    co_return;
//                }
//                else
//                {
//                    Json::Value json;
//                    json["result"] = "error";
//                    json["message"] =
//                            "Access interval should be at least 10 seconds";
//                    auto res = HttpResponse::newHttpJsonResponse(json);
//                    cb(res);
//                    co_return;
//                }
//            }
//            catch (const std::exception &err)
//            {
//                LOG_TRACE << "first visit,insert visitDate";
//                try
//                {
//                    // TODO
//                    //co_await updateCache(userid + "." VDate, now, redisClient);
//                }
//                catch (const std::exception &err)
//                {
//                    LOG_ERROR << err.what();
//                    cb(HttpResponse::newHttpJsonResponse(err.what()));
//                    co_return;
//                }
//                ccb();
//                co_return;
//            }
//        }
//        else
//        {
//            auto resp = HttpResponse::newNotFoundResponse();
//            cb(resp);
//            co_return;
//        }
        });
    }

}