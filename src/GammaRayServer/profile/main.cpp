//
// Created by RGAA on 19/08/2025.
//

#include <drogon/drogon.h>
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h>
#include <drogon/HttpAppFramework.h>
#include "gr_profile_context.h"
#include "gr_profile_settings.h"
#include "tc_common_new/log.h"

int main(int argc, char** argv) {
    // logs
    std::string log_path = "./gr_profile_server.log";
    tc::Logger::InitLog(log_path, true);
    LOGI("-------GrProfileServer Started-------");

    // settings
    auto settings = tc::GrProfileSettings::Instance();
    settings->LoadSettings();

    // context
    auto ctx = std::make_shared<tc::GrProfileContext>();
    if (!ctx->Init()) {
        return -1;
    }

    //drogon::app().createRedisClient("127.0.0.1", 6379);
    drogon::app()
        .setDocumentRoot("./static")
        .addListener("0.0.0.0", 8848)
        .run();

    return 0;
}