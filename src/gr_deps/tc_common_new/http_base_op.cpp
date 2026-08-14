//
// Created by RGAA on 27/03/2025.
//

#include "http_base_op.h"
#include "http_client.h"
#include "log.h"
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace tc
{

    Result<std::string, bool> HttpBaseOp::CanPingServer(bool ssl, const std::string& host, int port, const std::string& appkey) {
        auto client = ssl ? HttpClient::MakeSSL(host, port, "/ping", 2000) : HttpClient::Make(host, port, "/ping", 2000);
        auto resp = client->Request({
            {"appkey", appkey}
        });
        if (resp.status != 200 || resp.body.empty()) {
            LOGE("CanPingServer failed.");
            return TRError(false);
        }

        try {
            auto obj = json::parse(resp.body);
            if (obj["code"].get<int>() != 200) {
                return TRError(false);
            }
            return resp.body;
        } catch (...) {
            return TRError(false);
        }
    }

}