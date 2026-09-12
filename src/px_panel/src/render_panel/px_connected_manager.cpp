#include "px_connected_manager.h"

#include "px_app_messages.h"
#include "px_context.h"
#include "px_settings.h"

#include "px_common/message_notifier.h"

#include <Windows.h>

#include <functional>
#include <utility>

namespace px {

std::shared_ptr<PxConnectedManager> PxConnectedManager::Create(const std::shared_ptr<PxContext>& context) {
    auto manager = std::make_shared<PxConnectedManager>(context);
    manager->RegisterMessageListener();
    return manager;
}

PxConnectedManager::PxConnectedManager(std::shared_ptr<PxContext> context) : context_{std::move(context)} {}

PxConnectedManager::~PxConnectedManager() {
    if (messageListener_) {
        messageListener_->UnListenAll();
    }
}

int PxConnectedManager::ConnectedClientCount() const noexcept {
    return connectedClientCount_.load(std::memory_order_acquire);
}

void PxConnectedManager::RegisterMessageListener() {
    messageListener_ = context_->ObtainUIMessageListener();
    const std::weak_ptr<PxConnectedManager> weakSelf{shared_from_this()};
    messageListener_->Listen<MsgUpdateConnectedClientsInfo>([weakSelf](const MsgUpdateConnectedClientsInfo& message) {
        if (const auto self = weakSelf.lock()) {
            self->connectedClientCount_.store(static_cast<int>(message.clients_info_.size()), std::memory_order_release);
        }
    });
    messageListener_->Listen<MsgOneClientDisconnect>([weakSelf](const MsgOneClientDisconnect&) {
        if (const auto self = weakSelf.lock()) {
            const std::weak_ptr<PxConnectedManager> delayedSelf{self};
            self->context_->PostUIDelayTask([delayedSelf] {
                const auto manager = delayedSelf.lock();
                const std::reference_wrapper<PxSettings> settings{*PxSettings::Instance()};
                if (manager && manager->ConnectedClientCount() == 0 && settings.get().IsDisconnectAutoLockScreenEnabled()) {
                    ::LockWorkStation();
                }
            }, 6000);
        }
    });
}

} // namespace px
