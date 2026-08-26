//
// Created by RGAA on 21/10/2024.
//

#ifndef PX_SERVICE_CONTEXT_H
#define PX_SERVICE_CONTEXT_H

#include <atomic>
#include <memory>
#include <asio2/asio2.hpp>
#include "px_common_new/message_notifier.h"

namespace px
{

    class SharedPreference;

    class ServiceContext : public std::enable_shared_from_this<ServiceContext> {
    public:
        ServiceContext(int port);
        ~ServiceContext();

        void Start();
        void Exit();

        void PostBgTask(std::function<void()>&& task);
        std::shared_ptr<MessageListener> CreateMessageListener();
        std::shared_ptr<SharedPreference> GetSp() { return sp_; }
        int GetListeningPort() {return listening_port_;}
        std::string GetAppExeFolderPath();

        template<typename T>
        void SendAppMessage(const T& m) {
            if (msg_notifier_) {
                msg_notifier_->SendAppMessage(m);
            }
        }

    private:
        std::shared_ptr<asio2::timer> timer_ = nullptr;
        std::shared_ptr<asio2::iopool> iopool_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<SharedPreference> sp_;
        int listening_port_ = 0;
        std::atomic_bool started_{false};
        std::atomic_bool exiting_{false};
    };

}

#endif //PX_SERVICE_CONTEXT_H
