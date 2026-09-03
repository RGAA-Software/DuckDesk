//
// Created by RGAA on 2024/3/5.
//

#ifndef TC_PLUGIN_WS_FILE_TRANSFER_ROUTER_H
#define TC_PLUGIN_WS_FILE_TRANSFER_ROUTER_H

//#include "network/wss_router.h"
#include "network/ws_router.h"
#include "px_render/plugin_interface/px_net_plugin_type.h"
#include "px_common_new/file_transfer_send_result.h"
#include <atomic>
#include <mutex>

namespace px
{

    class Data;

    class WsFileTransferRouter : public WsRouter, public std::enable_shared_from_this<WsFileTransferRouter> {
    public:

        static std::shared_ptr<WsFileTransferRouter> Make(const WsDataPtr& data, bool only_audio, const std::string& device_id, const std::string& stream_id) {
            auto router = std::make_shared<WsFileTransferRouter>(data, only_audio);
            router->device_id_ = device_id;
            router->stream_id_ = stream_id;
            router->nt_channel_type_ = NetChannelType::kFileTransfer;
            return router;
        }

        explicit WsFileTransferRouter(const WsDataPtr& data, bool only_audio) : WsRouter(data){}
        void OnOpen(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnClose(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnMessage(std::shared_ptr<asio2::http_session> &sess_ptr, int64_t socket_fd, std::string_view data) override;
        void OnPing(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void OnPong(std::shared_ptr<asio2::http_session> &sess_ptr) override;
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;
        [[nodiscard]] FileTransferSendResult TryPostBinaryMessage(
            const std::shared_ptr<Data>& msg);

    private:
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireWritableSignal();
        void NotifyWritable();
        void NotifyClosed();

    public:
        std::string device_id_;
        std::string stream_id_;
        std::string logical_session_id_;
        std::string binding_id_;
        std::atomic_bool file_allowed_ = true;
        unsigned int post_thread_id_ = 0;
        NetChannelType nt_channel_type_;
        std::mutex writable_signal_mutex_;
        std::shared_ptr<FileTransferWritableSignal> writable_signal_;
    };

}

#endif //TC_APPLICATION_WS_MEDIA_ROUTER_H
