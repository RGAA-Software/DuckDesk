//
// Created by RGAA on 10/07/2025.
//

#include "stream_messages.h"
#include <nlohmann/json.hpp>
#include "render_panel/px_context.h"
#include "render_panel/px_application.h"

using namespace nlohmann;

namespace px
{

    std::string PxSmRestartRender::AsJson() {
        json obj;
        obj["event"] = "restart_render";
        obj["from_device"] = grApp->GetContext()->GetDeviceIdOrIpAddress();
        return obj.dump();
    }

    std::string PxSmLockScreen::AsJson() {
        json obj;
        obj["event"] = "lock_screen";
        obj["from_device"] = grApp->GetContext()->GetDeviceIdOrIpAddress();
        return obj.dump();
    }

    std::string PxSmRestartDevice::AsJson() {
        json obj;
        obj["event"] = "restart_device";
        obj["from_device"] = grApp->GetContext()->GetDeviceIdOrIpAddress();
        return obj.dump();
    }

    std::string PxSmShutdownDevice::AsJson() {
        json obj;
        obj["event"] = "shutdown_device";
        obj["from_device"] = grApp->GetContext()->GetDeviceIdOrIpAddress();
        return obj.dump();
    }
}