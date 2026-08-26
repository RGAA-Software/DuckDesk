//
// Created by RGAA on 27/02/2025.
//

#include "relay_context.h"

namespace px
{

    RelayContext::RelayContext(const std::shared_ptr<MessageNotifier>& notifier) {
        msg_notifier_ = notifier;
    }

    std::shared_ptr<MessageListener> RelayContext::CreateMessageListener(MessageExecutionLane lane) {
        return msg_notifier_->CreateListener(lane);
    }

}
