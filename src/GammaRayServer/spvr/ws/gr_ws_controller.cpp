//
// Created by RGAA on 19/08/2025.
//

#include "gr_ws_controller.h"

namespace tc
{

    void GrWsController::handleNewMessage(const WebSocketConnectionPtr& conn, std::string&& message, const WebSocketMessageType& type) {
        if (type == WebSocketMessageType::Ping) {

        }
        else if (type == WebSocketMessageType::Text) {
            auto &s = conn->getContextRef<Subscriber>();
            chatRooms_.publish(s.chatRoomName_, message);
        }
        else if (type == WebSocketMessageType::Binary) {

        }
    }

    void GrWsController::handleConnectionClosed(const WebSocketConnectionPtr& conn) {
        LOG_DEBUG << "websocket closed!";
        auto &s = conn->getContextRef<Subscriber>();
        chatRooms_.unsubscribe(s.chatRoomName_, s.id_);
    }

    void GrWsController::handleNewConnection(const HttpRequestPtr& req, const WebSocketConnectionPtr& conn) {
        LOG_DEBUG << "new websocket connection!";
        conn->send("haha!!!");
        Subscriber s;
        s.chatRoomName_ = req->getParameter("room_name");
        LOG_INFO << "from: " << req->getParameter("from");
        s.id_ = chatRooms_.subscribe(s.chatRoomName_,
                                     [conn](const std::string &topic,
                                            const std::string &message) {
                                         // Suppress unused variable warning
                                         (void) topic;
                                         conn->send(message);
                                     });
        conn->setContext(std::make_shared<Subscriber>(std::move(s)));
    }


}