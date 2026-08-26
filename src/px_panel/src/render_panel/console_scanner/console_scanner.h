//
// Created by RGAA on 23/10/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_SCANNER_H
#define GAMMARAYPREMIUM_CONSOLE_SCANNER_H

#include <map>
#include <memory>
#include <string>
#include <mutex>

namespace px
{

    class Thread;
    class PxApplication;
    class MessageListener;

    class StNetworkConsoleAccessInfo {
    public:
        std::string console_ip_;
        int console_port_;
        std::string relay_ip_;
        int relay_port_;
        std::string origin_info_;
        int64_t update_timestamp_ = 0;
        bool console_ssl_enable_ = true;
    };

    class ConsoleScanner : public std::enable_shared_from_this<ConsoleScanner> {
    public:
        explicit ConsoleScanner(const std::shared_ptr<PxApplication>& app);
        ~ConsoleScanner();
        //
        void StartUdpReceiver(int port);
        void Exit();
        std::map<std::string, std::shared_ptr<StNetworkConsoleAccessInfo>> GetConsoleAccessInfo();

    private:
        void ClearInactiveServer();

    private:
        std::weak_ptr<PxApplication> app_;
        // udp receiver thread
        std::shared_ptr<Thread> udp_receiver_thread_ = nullptr;
        std::atomic_bool exit_udp_receiver_ = false;
        std::mutex ac_mtx_;
        std::map<std::string, std::shared_ptr<StNetworkConsoleAccessInfo>> access_info_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_SCANNER_H
