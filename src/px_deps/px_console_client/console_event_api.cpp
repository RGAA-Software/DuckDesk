//
// Created by RGAA on 23/01/2026.
//

#include "console_event_api.h"
#include "console_http_client.h"
#include "console_server_info.h"
#include "console_errors.h"
#include "console_api.h"
#include <nlohmann/json.hpp>
#include "console_device.h"
#include "console_event.h"
#include "px_common_new/http_client.h"
#include "px_common_new/log.h"
#include "px_common_new/http_base_op.h"
#include "px_common_new/thread.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/hardware.h"
#include "px_common_new/ip_util.h"
#include "px_common_new/base64.h"
#include "px_common_new/uuid.h"

namespace px
{

    // /api/v1/event/control
    const std::string kConsoleEventControl = "/api/v1/event/control";

    // add
    const std::string kApiAddEvent = kConsoleEventControl + "/add";


    Result<ConsoleEventPtr, ConsoleApiError> ConsoleEventApi::AddEvent(const std::string& host,
                                                              int port,
                                                              const std::string& appkey,
                                                              const ConsoleEventPtr& event) {
        auto client = px_console::MakeConsoleHttpClient(host, port, kApiAddEvent, 2000);
        client->SetHeader("x-px-appkey", appkey);
        client->SetHeader("x-px-device-id", event->device_id_);

        const auto data = event->AsJson();
        auto resp = client->Post({}, data, "application/json");

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("AddCpuEvent failed: {}", resp.status);
            return TcErr(px_console::ToConsoleApiError(resp));
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            return event;
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
    }

}
