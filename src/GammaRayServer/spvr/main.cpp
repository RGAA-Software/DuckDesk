//
// Created by RGAA on 19/08/2025.
//

#include <drogon/drogon.h>
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpController.h>
#include "tc_common_new/log.h"
#include "gr_spvr_settings.h"
#include "gr_spvr_context.h"

int main(int argc, char** argv) {

    // logs
    std::string log_path = "./gr_spvr_server.log";
    tc::Logger::InitLog(log_path, true);
    LOGI("-------GrSpvrServer Started-------");

    // settings
    auto settings = tc::GrSpvrSettings::Instance();
    settings->LoadSettings();

    // context
    auto ctx = std::make_shared<tc::GrSpvrContext>();
    if (!ctx->Init()) {
        return -1;
    }

    drogon::app().createRedisClient("127.0.0.1", 6379);
    drogon::app()
        .setDocumentRoot("./static")
        .addListener("0.0.0.0", 8848)
        .run();

    return 0;
}