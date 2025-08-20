//
// Created by RGAA on 19/08/2025.
//

#include "gr_device_controller.h"
#include "spvr/gr_spvr_defs.h"
#include "spvr/gr_spvr_context.h"
#include "spvr/db/gr_device.h"
#include "spvr/db/gr_spvr_database.h"
#include "svr_base/gr_json_parser.h"
#include "svr_base/gr_http_response.h"

using namespace tc;

namespace api
{
    namespace v1
    {

        drogon::Task<drogon::HttpResponsePtr> GrDeviceController::CreateDevice(HttpRequestPtr req) const {
            auto body = std::string(req->body());
            LOG_INFO << "body: " << body;
            auto r = tc::ParseJsonFromString(body);
            if (!r.has_value()) {
                co_return tc::MakeErrParamResp();
            }

            auto params = r.value();
            if (!params.isMember(kParamReqInfo) || !params.isMember(kParamPlatform)) {
                co_return tc::MakeErrParamResp();
            }

            auto req_info = params[kParamReqInfo].asString();
            auto platform = params[kParamPlatform].asString();

            LOG_INFO << "req_info: " << req_info << ", platform: " << platform;
            auto db = grSpvrContext->GetDatabase();
            auto device = db->GenerateNewDevice(req_info, platform);

            Json::Value ret;
            ret["message"] = "Hello, World!";
            ret["id"] = device->device_id_;

            co_return MakeOkResp(ret);
        }

        drogon::Task<drogon::HttpResponsePtr> GrDeviceController::GetDeviceInfo(HttpRequestPtr req, std::string device_id) const {

            LOG_INFO << "the device id: " << device_id;
            auto resp = HttpResponse::newHttpResponse();
            nosql::RedisClientPtr redisClient = app().getRedisClient();
            auto r = co_await redisClient->execCommandCoro("hgetall %s", device_id.c_str());

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

        Task<HttpResponsePtr> GrDeviceController::GetAllDevicesInfo(HttpRequestPtr req) const {
            nosql::RedisClientPtr redisClient = app().getRedisClient();
            try {
                auto r = co_await redisClient->execCommandCoro("1000");
            } catch(const std::exception& e) {
                LOG_ERROR << "===> EXT: " << e.what();
            }
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k401Unauthorized);

//            drogon::async_run([]() -> Task<> {
//                nosql::RedisClientPtr redisClient = app().getRedisClient();
//                auto r = co_await redisClient->execCommandCoro("");
//            });

            co_return resp;
        }
    }
}