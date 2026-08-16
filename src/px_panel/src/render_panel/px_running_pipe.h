//
// Created by RGAA on 25/01/2025.
//

#ifndef PX_RUNNING_PIPE_H
#define PX_RUNNING_PIPE_H

#include <memory>
#include <thread>
#include <functional>
#include <Windows.h>

namespace px
{

    class PxRunningPipe {
    public:
        explicit PxRunningPipe();
        ~PxRunningPipe();
        void StartListening(std::function<void()>&& cbk);
        bool SendHello();

    private:
        std::shared_ptr<std::thread> recv_thread_ = nullptr;
        HANDLE recv_handle_ = nullptr;
        bool exit_receiving_ = false;
    };

}

#endif //PX_RUNNING_PIPE_H
