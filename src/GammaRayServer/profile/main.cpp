//
// Created by RGAA on 19/08/2025.
//

#include <drogon/drogon.h>
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h>
#include <drogon/HttpAppFramework.h>

int main(int argc, char** argv) {

    drogon::app().createRedisClient("127.0.0.1", 6379);
    drogon::app()
        .setDocumentRoot("./static")
        .addListener("0.0.0.0", 8848)
        .run();

    return 0;
}