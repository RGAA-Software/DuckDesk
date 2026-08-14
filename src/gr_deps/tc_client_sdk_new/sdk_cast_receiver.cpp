//
// Created by RGAA on 2024/3/15.
//

#include "sdk_cast_receiver.h"
#include "tc_common_new/log.h"

namespace tc
{

    std::shared_ptr<CastReceiver> CastReceiver::Make() {
        return std::make_shared<CastReceiver>();
    }

    CastReceiver::CastReceiver() {

    }

    void CastReceiver::Start() {
        recv_thread_ = std::thread([=, this]() {
            std::string_view host = "0.0.0.0";
            std::string_view port = "8030";
#if 0
            udp_cast_ = std::make_shared<asio2::udp_cast>();
            udp_cast_->bind_recv([&](asio::ip::udp::endpoint &endpoint, std::string_view data) {
                LOGI("recv : {} {} {} {} {}",
                       endpoint.address().to_string().c_str(), endpoint.port(),
                       data.size(), (int) data.size(), data.data());

                //udp_cast_->async_send(endpoint, data);
            }).bind_start([&]() {
                LOGI("start : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            }).bind_stop([&]() {
                LOGI("stop : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            }).bind_init([&]() {
                LOGI("--------------------init--------------------");
                //// Join the multicast group.
                udp_cast_->socket().set_option(
                	asio::ip::multicast::join_group(asio::ip::make_address("239.255.0.1")));
            });

            udp_cast_->start(host, port);
            LOGI("---------------------start--------------------");
            //udp_cast_->wait_stop();
#endif
        });
    }

    void CastReceiver::Exit() {
#if 0
        if (udp_cast_) {
            udp_cast_->stop();
        }
#endif
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }

}