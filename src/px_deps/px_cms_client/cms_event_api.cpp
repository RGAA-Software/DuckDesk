//
// Created by RGAA on 23/01/2026.
//

#include "cms_event_api.h"
#include "cms_server_info.h"
#include "cms_errors.h"
#include <nlohmann/json.hpp>
#include "cms_device.h"
#include "cms_event.h"
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
    const std::string kCmsEventControl = "/api/v1/event/control";

    // add
    const std::string kApiAddEvent = kCmsEventControl + "/add";


    Result<CmsEventPtr, CmsApiError> CmsEventApi::AddEvent(const std::string& host,
                                                              int port,
                                                              const std::string& appkey,
                                                              const CmsEventPtr& event) {
        auto client = HttpClient::MakeSSL(host, port, kApiAddEvent, 2000);

        const auto data = event->AsJson();
        auto resp = client->Post({
            {"appkey", appkey}
        }, data);

        if (resp.status != 200 || resp.body.empty()) {
            LOGE("AddCpuEvent failed: {}", resp.status);
            return TcErr((CmsApiError)resp.status);
        }

        try {
            auto json_obj = json::parse(resp.body)["data"];
            return event;
        }
        catch(std::exception& e) {
            LOGE("Parse json failed: {}", e.what());
            return TcErr(CmsApiError::kParseJsonFailed);
        }
    }

}