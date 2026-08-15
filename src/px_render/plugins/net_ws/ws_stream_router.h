//
// Created by RGAA on 2024/3/5.
//

#ifndef TC_APPLICATION_WS_PLUGIN_ROUTER_H
#define TC_APPLICATION_WS_PLUGIN_ROUTER_H

#include <mutex>
#include "network/ws_router.h"
#include "px_render/plugin_interface/gr_net_plugin_type.h"
//#include "network/wss_router.h"

namespace tc
{

    class Data;

    class WsStreamRouter : public WsRouter, public std::enable_shared_from_this<WsStreamRouter> {
    public:
        static std::shared_ptr<WsStreamRouter> Make(const WsDataPtr& data, bool only_audio, const std::string& visitor_device_id, const std::string& stream_id) {
            auto router = std::make_shared<WsStreamRouter>(data, only_audio);
            router->visitor_device_id_ = visitor_device_id;
            router->stream_id_ = stream_id;
            router->nt_channel_type_ = NetChannelType::kMedia;
            return router;
        }

        explicit WsStreamRouter(const WsDataPtr& data, bool only_audio) : WsRouter(data), enable_video_(!only_audio) {}
        void OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnMessage(std::shared_ptr<asio2::http_session> &sess_ptr, int64_t socket_fd, std::string_view data) override;
        void OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void PostBinaryMessage(std::shared_ptr<Data> data) override;
        void PostBinaryMessage(const std::string &data) override;
        void PostTextMessage(const std::string& data) override;

    public:
        bool enable_video_ = true;
        // udp_media=1 的客户端:媒体帧由 net_udp 插件裸 UDP 直发,本 ws 会话
        // 只承担控制面,kVideoFrame/kAudioFrame proto 不再下发(见 ws_server.cpp)
        bool udp_media_ = false;
        std::string visitor_device_id_;
        std::string stream_id_;
        unsigned int post_thread_id_ = 0;
        // written on the network thread (client hello), read on the statistics
        // thread (WsPluginServer::GetConnectedClientInfo), guarded by this mutex
        std::mutex device_name_mtx_;
        std::string device_name_;
        NetChannelType nt_channel_type_;
    };

}

#endif //TC_APPLICATION_WS_MEDIA_ROUTER_H
