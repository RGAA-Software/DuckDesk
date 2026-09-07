//
// Created by RGAA on 25/01/2025.
//

#ifndef PX_RUNNING_PIPE_H
#define PX_RUNNING_PIPE_H

#include <functional>
#include <memory>
#include <string>

namespace px
{
    class Thread;

    class PxRunningPipe {
    public:
        explicit PxRunningPipe(std::string pipe_name = R"(\\.\pipe\running\render_panel)");
        ~PxRunningPipe();
        void StartListening(std::function<void()>&& cbk);
        void StopListening();
        bool SendHello();

    private:
        class State;

        static void ReceiveLoop(const std::shared_ptr<State>& state);

        std::string pipe_name_;
        std::shared_ptr<State> state_;
        std::shared_ptr<Thread> recv_thread_;
    };

}

#endif //PX_RUNNING_PIPE_H
