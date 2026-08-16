//
// Created by RGAA on 27/02/2025.
//

#ifndef PX_MGR_CONTEXT_H
#define PX_MGR_CONTEXT_H

#include "px_common_new/message_notifier.h"

namespace px
{

    class RelayContext {
    public:
        explicit RelayContext(const std::shared_ptr<MessageNotifier>& notifier);

        std::shared_ptr<MessageListener> CreateMessageListener();

        template<typename T>
        void SendAppMessage(const T& m) {
            if (msg_notifier_) {
                msg_notifier_->SendAppMessage(m);
            }
        }

    private:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;

    };

}

#endif //PX_MGR_CONTEXT_H
