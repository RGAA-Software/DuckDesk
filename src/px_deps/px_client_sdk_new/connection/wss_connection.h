//
// Created by RGAA on 8/12/2024.
//

#ifndef GAMMARAYPC_WSS_CONNECTION_H
#define GAMMARAYPC_WSS_CONNECTION_H

#include "connection.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

namespace asio2 {
    class wss_client;
    class timer;
}

namespace px
{
    class PxAsyncScope;
    class PxReconnectSupervisor;

    class WssConnection : public Connection,
                          public std::enable_shared_from_this<WssConnection> {
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
        [[nodiscard]] std::uint64_t ConnectionGeneration() const;

    private:
        std::shared_ptr<PxAsyncScope> BeginStop();
        void FinishStop();
        void ScheduleDeferredStop();

        std::string host_{};
        int port_{0};
        std::string path_{};
        std::shared_ptr<asio2::wss_client> client_{};
        std::shared_ptr<PxAsyncScope> async_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::atomic_bool terminal_rejection_{false};
        std::atomic_bool started_{false};
        std::atomic_bool exiting_{false};
        std::atomic_bool deferred_stop_scheduled_{false};
        std::mutex stop_mutex_{};

    };

}

#endif //GAMMARAYPC_WS_CONNECTION_H
