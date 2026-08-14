//
// Created by RGAA on 12/12/2025.
//

#include "spvr_api.h"

#include "spvr_device.h"
#include "tc_common_new/log.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/http_base_op.h"
#include <nlohmann/json.hpp>

using namespace tc;

namespace spvr
{

    const std::string kSpvrControl = "/api/v1/spvr/control";
    const std::string kQueryAliveConnections = kSpvrControl + "/query/alive/connections";
    const std::string kQueryAvailableNewConnection = kSpvrControl + "/available/new/connection";

    // Convert a failed http response to a meaningful SpvrApiError.
    // The CMS returns error responses as a json body: {code, message, data},
    // where code is a business code (600+); prefer it (and its message) when present.
    // Otherwise map the http status explicitly instead of casting it to a business code:
    //   401 -> authorization invalid/expired (appkey or license check failed)
    //   403 -> max streams reached, no available connection
    static SpvrApiError ToSpvrApiError(const tc::HttpResponse& resp) {
        SetSpvrApiLastErrorMessage("");
        if (!resp.body.empty()) {
            try {
                auto obj = json::parse(resp.body);
                auto code = obj["code"].get<int>();
                if (code >= (int)SpvrApiError::kInvalidParams) {
                    SetSpvrApiLastErrorMessage(obj.value("message", ""));
                    return (SpvrApiError)code;
                }
            }
            catch (const std::exception& e) {
                LOGE("ToSpvrApiError parse failed: {}, body: {}", e.what(), resp.body);
            }
        }
        if (resp.status == 401) {
            return SpvrApiError::kInvalidAppkey;
        }
        if (resp.status == 403) {
            return SpvrApiError::kMaxStreamsReached;
        }
        return SpvrApiError::kInternalError;
    }

    tc::Result<AliveConnections, SpvrApiError>
    SpvrApi::QueryAliveConnections(const std::string& host,
                                   int port,
                                   const std::string& appkey) {
        const auto client = HttpClient::MakeSSL(host, port, kQueryAliveConnections, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAliveConnections failed: {}", resp.status);
            return TcErr(ToSpvrApiError(resp));
        }

        try {
            json obj = json::parse(resp.body)[kData];
            int total = obj["total"].get<int>();
            int relay = obj["relay"].get<int>();
            return AliveConnections {
                .total_ = total,
                .relay_ = relay,
            };
        }
        catch (const std::exception& e) {
            LOGE("QueryUserBindDevices parse failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

    tc::Result<AvailableNewConnection, SpvrApiError>
    SpvrApi::QueryAvailableNewConnection(const std::string& host, int port, const std::string& appkey) {
        const auto client = HttpClient::MakeSSL(host, port, kQueryAvailableNewConnection, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAvailableNewConnection failed: {}", resp.status);
            return TcErr(ToSpvrApiError(resp));
        }

        try {
            json obj = json::parse(resp.body)[kData];
            auto available = obj["available"].get<bool>();
            return AvailableNewConnection {
                .available_ = available,
            };
        }
        catch (const std::exception& e) {
            LOGE("QueryUserBindDevices parse failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

}