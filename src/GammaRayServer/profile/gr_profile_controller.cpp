//
// Created by RGAA on 19/08/2025.
//

#include "gr_profile_controller.h"

namespace api
{
    namespace v1
    {

        drogon::Task<drogon::HttpResponsePtr> GrProfileController::AddProfile(HttpRequestPtr req) const {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setBody("OK AddProfile");
            co_return resp;
        }

        drogon::Task<drogon::HttpResponsePtr> GrProfileController::GetProfileInfo(HttpRequestPtr req, std::string profile_id) const {

            LOG_INFO << "the device id: " << profile_id;
            auto resp = HttpResponse::newHttpResponse();
            nosql::RedisClientPtr redisClient = app().getRedisClient();
            auto r = co_await redisClient->execCommandCoro("hgetall %s", profile_id.c_str());

            if (r.type() == nosql::RedisResultType::kNil) {
                resp->setStatusCode(k404NotFound);
                co_return resp;
            }

            auto array = r.asArray();
            for (const auto& a : array) {
                LOG_INFO << "redis resp: " << a.asString();
            }

            resp->setStatusCode(k200OK);
            co_return resp;
        }

        Task<HttpResponsePtr> GrProfileController::GetProfilesInfo(HttpRequestPtr req, int page, int page_size) const {
            nosql::RedisClientPtr redisClient = app().getRedisClient();
            try {
                auto r = co_await redisClient->execCommandCoro("1000");
            } catch(const std::exception& e) {
                e.what();
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k401Unauthorized);

//            drogon::async_run([]() -> Task<> {
//                nosql::RedisClientPtr redisClient = app().getRedisClient();
//                auto r = co_await redisClient->execCommandCoro("");
//            });

            Json::Value ret;
//            ret["message"] = "Hello, World!";
//            auto resp = HttpResponse::newHttpJsonResponse(ret);
//            callback(resp);

            co_return resp;
        }
    }
}