//
// Created by RGAA on 12/12/2025.
//

#include "console_api.h"
#include "console_http_client.h"

#include "console_device.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include <nlohmann/json.hpp>

using namespace px;

namespace px_console
{

    const std::string kConsoleControl = "/api/v1/console/control";
    const std::string kQueryAliveConnections = kConsoleControl + "/query/alive/connections";
    const std::string kQueryAvailableNewConnection = kConsoleControl + "/available/new/connection";

    // Convert a failed http response to a meaningful ConsoleApiError.
    // The Console returns error responses as a json body: {code, message, data},
    // where code is a business code (600+); prefer it (and its message) when present.
    // Otherwise map the http status explicitly instead of casting it to a business code:
    //   401 -> authorization invalid/expired (appkey or license check failed)
    //   403 -> max streams reached, no available connection
    static ConsoleApiError ToConsoleApiError(const px::HttpResponse& resp) {
        SetConsoleApiLastErrorMessage("");
        if (!resp.body.empty()) {
            try {
                auto obj = json::parse(resp.body);
                auto code = obj["code"].get<int>();
                if (code >= (int)ConsoleApiError::kInvalidParams) {
                    SetConsoleApiLastErrorMessage(obj.value("message", ""));
                    return (ConsoleApiError)code;
                }
            }
            catch (const std::exception& e) {
                LOGE("ToConsoleApiError parse failed: {}, body: {}", e.what(), resp.body);
            }
        }
        if (resp.status == 401) {
            return ConsoleApiError::kInvalidAppkey;
        }
        if (resp.status == 403) {
            return ConsoleApiError::kMaxStreamsReached;
        }
        return ConsoleApiError::kInternalError;
    }

    px::Result<AliveConnections, ConsoleApiError>
    ConsoleApi::QueryAliveConnections(const std::string& host,
                                   int port,
                                   const std::string& appkey) {
        const auto client = MakeConsoleHttpClient(host, port, kQueryAliveConnections, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAliveConnections failed: {}", resp.status);
            return TcErr(ToConsoleApiError(resp));
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
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

    px::Result<AvailableNewConnection, ConsoleApiError>
    ConsoleApi::QueryAvailableNewConnection(const std::string& host, int port, const std::string& appkey) {
        const auto client = MakeConsoleHttpClient(host, port, kQueryAvailableNewConnection, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAvailableNewConnection failed: {}", resp.status);
            return TcErr(ToConsoleApiError(resp));
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
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

}