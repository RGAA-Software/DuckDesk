//
// Created by RGAA on 2024/1/24.
//

#pragma once

#include <memory>

#include "inject_params.h"

namespace tc
{

    class ClientManager {
    public:

        static ClientManager* Instance() {
            static ClientManager instance;
            return &instance;
        }

        std::shared_ptr<InjectParams> GetInjectParams();
        void CopyUserData(const uint8_t* buffer, int size);

    private:

        std::shared_ptr<InjectParams> params_ = nullptr;

    };

}
