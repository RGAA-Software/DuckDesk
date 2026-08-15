//
// Created by RGAA on 8/12/2024.
//

#ifndef GAMMARAYPC_UDP_CONNECTION_H
#define GAMMARAYPC_UDP_CONNECTION_H

#include "connection.h"
#include <memory>

namespace asio2
{
    class udp_client;
}

namespace tc
{

    class UdpConnection : public Connection {
    public:
        UdpConnection(const std::shared_ptr<ThunderSdkParams>& params, const std::shared_ptr<MessageNotifier>& notifier, const std::string& host, int port);
        void Start() override;
        void Stop() override;
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;

    private:
        std::string host_;
        int port_;
        std::shared_ptr<asio2::udp_client> udp_client_ = nullptr;
    };

}

#endif //GAMMARAYPC_UDP_CONNECTION_H
