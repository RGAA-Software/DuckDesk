//
// Created by RGAA on 2024/3/5.
//

#include "ws_media_router.h"
#include "px_common/data.h"
#include "px_common/log.h"
#include "rd_statistics.h"

namespace px
{

void WsMediaRouter::OnOpen(std::shared_ptr<asio2::http_session>& session) {
    WsRouter::OnOpen(session);
}

void WsMediaRouter::OnClose(std::shared_ptr<asio2::http_session>& session) {
    WsRouter::OnClose(session);
}

void WsMediaRouter::OnMessage(std::shared_ptr<asio2::http_session>& session,
                              int64_t socket_fd, std::string_view payload) {
    WsRouter::OnMessage(session, socket_fd, payload);
    // Get<std::shared_ptr<MessageProcessor>>("proc")->HandleMessage(shared_from_this(),
    // data);
}

void WsMediaRouter::OnPing(std::shared_ptr<asio2::http_session>& session) {
    WsRouter::OnPing(session);
}

void WsMediaRouter::OnPong(std::shared_ptr<asio2::http_session>& session) {
    WsRouter::OnPong(session);
}

void WsMediaRouter::PostBinaryMessage(std::shared_ptr<Data> payload) {
    this->PostBinaryMessage(payload->AsString());
}

void WsMediaRouter::PostBinaryMessage(const std::string& payload) {
    if (session_ && session_->is_started()) {
        auto weak_self = weak_from_this();
        session_->post_queued_event([weak_self, payload]() {
            auto self = weak_self.lock();
            if (!self || !self->session_ || !self->session_->is_started()) {
                return;
            }
            self->session_->ws_stream().binary(true);
            self->queuing_message_count_++;
            self->session_->async_send(payload, [weak_self](size_t bytes_sent) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                RdStatistics::Instance()->AppendMediaBytes(bytes_sent);
                self->queuing_message_count_--;
            });
        });
    }
}

void WsMediaRouter::PostTextMessage(const std::string& message) {
    if (session_ && session_->is_started()) {
        auto weak_self = weak_from_this();
        session_->post_queued_event([weak_self, message]() {
            auto self = weak_self.lock();
            if (!self || !self->session_ || !self->session_->is_started()) {
                return;
            }
            self->session_->ws_stream().text(true);
            self->queuing_message_count_++;
            self->session_->async_send(message, [weak_self](size_t bytes_sent) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                RdStatistics::Instance()->AppendMediaBytes(bytes_sent);
                self->queuing_message_count_--;
            });
        });
    }
}
}
