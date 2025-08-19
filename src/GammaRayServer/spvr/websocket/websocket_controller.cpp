//
// Created by RGAA on 19/08/2025.
//

#include "websocket_controller.h"

namespace tc
{

    void WebSocketChat::handleNewMessage(const WebSocketConnectionPtr &wsConnPtr,
                                         std::string &&message,
                                         const WebSocketMessageType &type) {
        // write your application logic here
        LOG_DEBUG << "new websocket message:" << message;
        if (type == WebSocketMessageType::Ping) {
            LOG_DEBUG << "recv a ping";
        } else if (type == WebSocketMessageType::Text) {
            auto &s = wsConnPtr->getContextRef<Subscriber>();
            chatRooms_.publish(s.chatRoomName_, message);
        }
    }

    void WebSocketChat::handleConnectionClosed(const WebSocketConnectionPtr &conn) {
        LOG_DEBUG << "websocket closed!";
        auto &s = conn->getContextRef<Subscriber>();
        chatRooms_.unsubscribe(s.chatRoomName_, s.id_);
    }

    void WebSocketChat::handleNewConnection(const HttpRequestPtr &req,
                                            const WebSocketConnectionPtr &conn) {
        LOG_DEBUG << "new websocket connection!";
        conn->send("haha!!!");
        Subscriber s;
        s.chatRoomName_ = req->getParameter("room_name");
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