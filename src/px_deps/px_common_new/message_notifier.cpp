//
// Created by RGAA on 2024/1/16.
//

#include "message_notifier.h"

namespace tc
{

    MessageNotifier::MessageNotifier() {
        event_bus_ = std::make_shared<dexode::EventBus>();
    }

    MessageNotifier::~MessageNotifier() {

    }

    std::shared_ptr<MessageListener> MessageNotifier::CreateListener() {
        auto inner = std::make_shared<dexode::EventBus::Listener>(event_bus_);
        auto listener = std::make_shared<MessageListener>(inner);
        return listener;
    }

}
