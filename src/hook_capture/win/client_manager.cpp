//
// Created by RGAA on 2024/1/24.
//

#include "client_manager.h"

namespace tc
{

    void ClientManager::CopyUserData(const uint8_t* buffer, int size) {
        params_ = std::make_shared<InjectParams>();
        memset(params_.get(), 0, sizeof(InjectParams));
        bool has_user_data = buffer != nullptr;
        if (has_user_data) {
            memcpy(params_.get(), buffer, size);
        }
    }

    std::shared_ptr<InjectParams> ClientManager::GetInjectParams() {
        return params_;
    }

}