//
// Created by RGAA on 8/12/2024.
//

#ifndef GAMMARAYPC_WS_LOCAL_CLIENT_H
#define GAMMARAYPC_WS_LOCAL_CLIENT_H

#include <string>
#include <memory>
#include <atomic>

namespace asio2 {
    class ws_client;
    class timer;
}

namespace tc
{

    class Data;

    class WsLocalClient {
    public:
        WsLocalClient(const std::string& host, int port, const std::string& path);
        ~WsLocalClient();
        void Start();
        void Stop();
        void PostBinaryMessage(std::shared_ptr<Data> msg);

    private:
        std::string host_;
        int port_ = 0;
        std::string path_;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::atomic_uint32_t queuing_message_count_ = 0;
    };

}

#endif //GAMMARAYPC_WS_CONNECTION_H
