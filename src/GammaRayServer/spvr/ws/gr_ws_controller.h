//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_WS_CONTROLLER_H
#define GAMMARAYPREMIUM_GR_WS_CONTROLLER_H

#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h>
#include <drogon/HttpAppFramework.h>
using namespace drogon;

namespace tc
{

    class GrWsController : public drogon::WebSocketController<GrWsController> {
    public:
        void handleNewMessage(const WebSocketConnectionPtr&, std::string&&, const WebSocketMessageType&) override;
        void handleConnectionClosed(const WebSocketConnectionPtr &) override;
        void handleNewConnection(const HttpRequestPtr&, const WebSocketConnectionPtr&) override;

        WS_PATH_LIST_BEGIN
            WS_PATH_ADD("/spvr/inner", Get);
            WS_ADD_PATH_VIA_REGEX("/[^/]*", Get);
        WS_PATH_LIST_END

    private:
        PubSubService<std::string> chatRooms_;
    };

    struct Subscriber {
        std::string chatRoomName_;
        drogon::SubscriberID id_;
    };
}

#endif //GAMMARAYPREMIUM_GR_WS_CONTROLLER_H
