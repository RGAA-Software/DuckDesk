//
// Created by RGAA on 1/03/2025.
//

#ifndef PX_RELAY_CONNECTION_H
#define PX_RELAY_CONNECTION_H

#include "connection.h"
#include <memory>

namespace asio2
{
    class udp_client;
}

namespace px
{

    class RelayClientSdk;
    class RelayNetClient;

    class RelayConnection : public Connection,
                            public std::enable_shared_from_this<RelayConnection> {
    public:
        RelayConnection(const std::shared_ptr<ThunderSdkParams>& params,
                        const std::shared_ptr<MessageNotifier>& notifier,
                        const std::string& host,
                        int port,
                        const std::string& device_id,
                        const std::string& remote_device_id,
                        bool auto_relay,
                        const std::string& room_type);
        ~RelayConnection() override;
        void Start() override;
        void Stop() override;
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;
        int64_t GetQueuingMsgCount() override;
        void RequestPauseStream() override;
        void RequestResumeStream() override;
        void RetryConnection() override;
        void On16msTimeout() override;
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal() override;

    private:
        std::string host_;
        int port_{0};
        std::string device_id_;
        std::string remote_device_id_;
        std::shared_ptr<RelayClientSdk> relay_sdk_ = nullptr;
        bool auto_relay_ = false;
        std::string room_type_;
    };

}

#endif //PX_RELAY_CONNECTION_H
