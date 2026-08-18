//
// Created by RGAA on 12/12/2025.
//

#include "cms_api.h"
#include "cms_http_client.h"

#include "cms_device.h"
#include "px_common_new/log.h"
#include "px_common_new/http_client.h"
#include "px_common_new/http_base_op.h"
#include <nlohmann/json.hpp>

using namespace px;

namespace px_cms
{

    const std::string kCmsControl = "/api/v1/cms/control";
    const std::string kQueryAliveConnections = kCmsControl + "/query/alive/connections";
    const std::string kQueryAvailableNewConnection = kCmsControl + "/available/new/connection";

    // Convert a failed http response to a meaningful CmsApiError.
    // The CMS returns error responses as a json body: {code, message, data},
    // where code is a business code (600+); prefer it (and its message) when present.
    // Otherwise map the http status explicitly instead of casting it to a business code:
    //   401 -> authorization invalid/expired (appkey or license check failed)
    //   403 -> max streams reached, no available connection
    static CmsApiError ToCmsApiError(const px::HttpResponse& resp) {
        SetCmsApiLastErrorMessage("");
        if (!resp.body.empty()) {
            try {
                auto obj = json::parse(resp.body);
                auto code = obj["code"].get<int>();
                if (code >= (int)CmsApiError::kInvalidParams) {
                    SetCmsApiLastErrorMessage(obj.value("message", ""));
                    return (CmsApiError)code;
                }
            }
            catch (const std::exception& e) {
                LOGE("ToCmsApiError parse failed: {}, body: {}", e.what(), resp.body);
            }
        }
        if (resp.status == 401) {
            return CmsApiError::kInvalidAppkey;
        }
        if (resp.status == 403) {
            return CmsApiError::kMaxStreamsReached;
        }
        return CmsApiError::kInternalError;
    }

    px::Result<AliveConnections, CmsApiError>
    CmsApi::QueryAliveConnections(const std::string& host,
                                   int port,
                                   const std::string& appkey) {
        const auto client = MakeCmsHttpClient(host, port, kQueryAliveConnections, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAliveConnections failed: {}", resp.status);
            return TcErr(ToCmsApiError(resp));
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
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

    px::Result<AvailableNewConnection, CmsApiError>
    CmsApi::QueryAvailableNewConnection(const std::string& host, int port, const std::string& appkey) {
        const auto client = MakeCmsHttpClient(host, port, kQueryAvailableNewConnection, 2000);
        auto resp = client->Request({
            {"appkey", appkey},
        });

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("QueryAvailableNewConnection failed: {}", resp.status);
            return TcErr(ToCmsApiError(resp));
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
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

}