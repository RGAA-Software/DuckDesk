//
// Created by RGAA on 23/01/2026.
//

#include "spvr_event_api.h"
#include "spvr_server_info.h"
#include "spvr_errors.h"
#include <nlohmann/json.hpp>
#include "spvr_device.h"
#include "spvr_event.h"
#include "tc_common_new/http_client.h"
#include "tc_common_new/log.h"
#include "tc_common_new/http_base_op.h"
#include "tc_common_new/thread.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/hardware.h"
#include "tc_common_new/ip_util.h"
#include "tc_common_new/base64.h"
#include "tc_common_new/uuid.h"

namespace tc
{

    // /api/v1/event/control
    const std::string kSpvrEventControl = "/api/v1/event/control";

    // add
    const std::string kApiAddEvent = kSpvrEventControl + "/add";


    Result<SpvrEventPtr, SpvrApiError> SpvrEventApi::AddEvent(const std::string& host,
                                                              int port,
                                                              const std::string& appkey,
                                                              const SpvrEventPtr& event) {
        auto client = HttpClient::MakeSSL(host, port, kApiAddEvent, 2000);

        const auto data = event->AsJson();
        auto resp = client->Post({
            {"appkey", appkey}
        }, data);

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("AddCpuEvent failed: {}", resp.status);
            return TcErr((SpvrApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            return event;
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(SpvrApiError::kParseJsonFailed);
        }
    }

}