//
// Created by RGAA on 8/12/2024.
//

#ifndef GAMMARAYPC_WSS_CONNECTION_H
#define GAMMARAYPC_WSS_CONNECTION_H

#include "connection.h"

#include <memory>

namespace asio2 {
    class wss_client;
    class timer;
}

namespace tc
{

    class WssConnection : public Connection {
    public:
        WssConnection(const std::shared_ptr<ThunderSdkParams>& params,
                     const std::shared_ptr<MessageNotifier>& notifier,
                     const std::string& host,
                     int port,
                     const std::string& path);
        ~WssConnection() override;
        void Start() override;
        void Stop() override;
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;
        void PostTextMessage(const std::string& msg) override;
        bool IsAlive() override;

    private:
        std::string host_;
        int port_ = 0;
        std::string path_;
        std::shared_ptr<asio2::wss_client> client_ = nullptr;

    };

}

#endif //GAMMARAYPC_WS_CONNECTION_H
