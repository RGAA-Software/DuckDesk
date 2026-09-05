//
// Created by RGAA on 12/12/2025.
//

#include "console_api.h"
#include "console_http_client.h"

#include "console_device.h"
#include "px_common/log.h"
#include "px_common/http_client.h"
#include "px_common/http_base_op.h"
#include <nlohmann/json.hpp>

using namespace px;

namespace px_console
{

    namespace {

        ConsoleApiError ParseConsoleHttpError(const px::HttpResponse& response,
                                              ConsoleApiError unauthorized_error,
                                              ConsoleApiError forbidden_error) {
            SetConsoleApiLastErrorMessage("");
            if (!response.body.empty()) {
                try {
                    const auto object = json::parse(response.body);
                    const auto code = object.value("code", 0);
                    SetConsoleApiLastErrorMessage(object.value("message", ""));
                    if (code >= static_cast<int>(ConsoleApiError::kInvalidParams)) {
                        return static_cast<ConsoleApiError>(code);
                    }
                }
                catch (const std::exception& error) {
                    LOGE("Parse Console error response failed: {}, body: {}", error.what(), response.body);
                }
            }

            if (response.status <= 0) {
                SetConsoleApiLastErrorMessage(response.error_message.empty()
                    ? "The Console did not return a response."
                    : response.error_message);
                return ConsoleApiError::kNetworkUnavailable;
            }

            switch (response.status) {
                case 401: return unauthorized_error;
                case 403: return forbidden_error;
                case 404: return ConsoleApiError::kNotFound;
                case 409: return ConsoleApiError::kConflict;
                case 410: return ConsoleApiError::kGone;
                case 429: return ConsoleApiError::kRateLimited;
                case 503: return ConsoleApiError::kServiceUnavailable;
                default:
                    if (ConsoleApiLastErrorMessage().empty()) {
                        SetConsoleApiLastErrorMessage("HTTP " + std::to_string(response.status));
                    }
                    return ConsoleApiError::kInternalError;
            }
        }

    }

    const std::string kConsoleControl = "/api/v1/console/control";
    const std::string kQueryAliveConnections = kConsoleControl + "/query/alive/connections";
    const std::string kQueryAvailableNewConnection = kConsoleControl + "/available/new/connection";

    // Convert a failed http response to a meaningful ConsoleApiError.
    // The Console returns error responses as a json body: {code, message, data},
    // where code is a business code (600+); prefer it (and its message) when present.
    // Otherwise map the http status explicitly instead of casting it to a business code:
    //   401 -> authorization invalid/expired (appkey or license check failed)
    //   403 -> max streams reached, no available connection
    ConsoleApiError ToConsoleApiError(const px::HttpResponse& resp) {
        return ParseConsoleHttpError(resp, ConsoleApiError::kInvalidAppkey,
                                     ConsoleApiError::kMaxStreamsReached);
    }

    ConsoleApiError ToConsoleUserApiError(const px::HttpResponse& response) {
        return ParseConsoleHttpError(response, ConsoleApiError::kAuthenticationRequired,
                                     ConsoleApiError::kForbidden);
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
