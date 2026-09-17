//
// Created by RGAA on 2024/3/5.
//

#ifndef TC_APPLICATION_WSS_ROUTER_H
#define TC_APPLICATION_WSS_ROUTER_H

#include <any>
#include <asio2/asio2.hpp>
#include <atomic>
#include <functional>
#include <map>
#include <string>

#include "px_common/time_util.h"
#include "px_common/uuid.h"
#include "ws_data.h"

namespace px {
class Data;

class WssRouter {
   public:
    explicit WssRouter(const WsDataPtr& ws_data) {
        ws_data_ = ws_data;
        created_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        connection_id_ = MD5::Hex(GetUUID());
    }

    virtual void OnOpen(std::shared_ptr<asio2::https_session>& session) {
        session_ = session;
    }

    virtual void OnClose(std::shared_ptr<asio2::https_session>& session) {
        session_ = nullptr;
    }

    virtual void OnMessage(std::shared_ptr<asio2::https_session>& session,
                           int64_t socket_fd, std::string_view payload) {}

    virtual void OnPing(std::shared_ptr<asio2::https_session>& session) {}

    virtual void OnPong(std::shared_ptr<asio2::https_session>& session) {}

    virtual void PostBinaryMessage(std::shared_ptr<Data> payload) {}

    virtual void PostBinaryMessage(const std::string& payload) {}

    virtual void PostTextMessage(const std::string& message) {}

    virtual int64_t GetQueuingMsgCount() { return queuing_message_count_; }

   protected:
    template <typename T>
    T Get(const std::string& variable_name) {
        auto variable_value = ws_data_->vars_[variable_name];
        return std::any_cast<T>(variable_value);
    }

   protected:
    std::shared_ptr<WsData> ws_data_ = nullptr;
    std::shared_ptr<asio2::https_session> session_ = nullptr;
    std::atomic_int64_t queuing_message_count_ = 0;

   public:
    bool enable_audio_ = false;
    bool enable_video_ = false;
    int64_t created_timestamp_ = 0;
    // random id for this connection
    // 1. used for logging records
    std::string connection_id_;
};

using WssRouterPtr = std::shared_ptr<WssRouter>;

}  // namespace px

#endif  // TC_APPLICATION_WS_ROUTER_H
