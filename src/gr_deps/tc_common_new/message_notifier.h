//
// Created by RGAA on 2024/1/16.
//

#ifndef TC_APPLICATION_MESSAGE_NOTIFIER_H
#define TC_APPLICATION_MESSAGE_NOTIFIER_H

#include <memory>
#include <functional>
#ifdef WIN32
#undef min
#undef max
#endif
#include "dexode/EventBus.hpp"

namespace tc
{
    class MessageListener {
    public:

        explicit MessageListener(const std::shared_ptr<dexode::EventBus::Listener>& l) : listener_(l) {}

        template<typename T>
        void Listen(std::function<void(const T&)>&& cbk) {
            listener_->listen([=](const T& msg) {
                cbk(msg);
            });
        }

        void UnListenAll() const {
            if (listener_) {
                listener_->unlistenAll();
            }
        }

    private:
        std::shared_ptr<dexode::EventBus::Listener> listener_ = nullptr;
    };

    class MessageNotifier {
    public:

        MessageNotifier();
        ~MessageNotifier();

        std::shared_ptr<MessageListener> CreateListener();

        template<typename T>
        void SendAppMessage(const T& m) {
            event_bus_->postpone(m);
            event_bus_->process();
        }

    private:

        std::shared_ptr<dexode::EventBus> event_bus_ = nullptr;

    };
}

#endif //TC_APPLICATION_MESSAGE_NOTIFIER_H
