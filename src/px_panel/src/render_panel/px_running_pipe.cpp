//
// Created by RGAA on 25/01/2025.
//

#include "px_running_pipe.h"

#include <Windows.h>
#include <atlbase.h>

#include <array>
#include <atomic>
#include <string_view>
#include <utility>

#include "px_common/log.h"
#include "px_common/thread.h"

namespace px
{
    namespace {
        constexpr std::string_view kPipeSignal = "Good";
        constexpr DWORD kStopPollMs = 50;

        bool WaitForOverlapped(
            const CHandle& pipe,
            const CHandle& operation_event,
            OVERLAPPED& operation,
            const std::atomic_bool& stopping,
            DWORD& transferred) {
            for (;;) {
                const DWORD wait_result = WaitForSingleObject(operation_event, kStopPollMs);
                if (wait_result == WAIT_OBJECT_0) {
                    return GetOverlappedResult(pipe, &operation, &transferred, FALSE) != FALSE;
                }
                if (wait_result != WAIT_TIMEOUT || stopping.load(std::memory_order_acquire)) {
                    CancelIoEx(pipe, &operation);
                    WaitForSingleObject(operation_event, INFINITE);
                    GetOverlappedResult(pipe, &operation, &transferred, FALSE);
                    return false;
                }
            }
        }

        bool ConnectClient(const CHandle& pipe, const std::atomic_bool& stopping) {
            CHandle operation_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!operation_event) {
                return false;
            }
            OVERLAPPED operation{};
            operation.hEvent = operation_event;
            if (ConnectNamedPipe(pipe, &operation)) {
                return true;
            }
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                return true;
            }
            if (error != ERROR_IO_PENDING) {
                LOGE("ConnectNamedPipe failed, error={}", error);
                return false;
            }
            DWORD transferred = 0;
            return WaitForOverlapped(
                pipe, operation_event, operation, stopping, transferred);
        }

        bool ReadSignal(
            const CHandle& pipe,
            const std::atomic_bool& stopping,
            std::string& signal) {
            CHandle operation_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!operation_event) {
                return false;
            }
            OVERLAPPED operation{};
            operation.hEvent = operation_event;
            std::array<char, 1024> buffer{};
            DWORD transferred = 0;
            if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                          &transferred, &operation)) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING ||
                    !WaitForOverlapped(
                        pipe, operation_event, operation, stopping, transferred)) {
                    return false;
                }
            }
            signal.assign(buffer.data(), transferred);
            return true;
        }
    }

    class PxRunningPipe::State {
    public:
        State(std::string name, std::function<void()> receive_callback)
            : pipe_name(std::move(name)), callback(std::move(receive_callback)) {}

        std::string pipe_name;
        std::function<void()> callback;
        std::atomic_bool stopping{false};
    };

    PxRunningPipe::PxRunningPipe(std::string pipe_name)
        : pipe_name_(std::move(pipe_name)) {}

    PxRunningPipe::~PxRunningPipe() {
        StopListening();
    }

    void PxRunningPipe::StartListening(std::function<void()>&& cbk) {
        StopListening();
        state_ = std::make_shared<State>(pipe_name_, std::move(cbk));
        const auto state = state_;
        recv_thread_ = Thread::MakeOnceTask(
            [state]() { ReceiveLoop(state); }, "panel_running_pipe", false);
    }

    void PxRunningPipe::StopListening() {
        const auto state = state_;
        if (state) {
            state->stopping.store(true, std::memory_order_release);
        }
        const auto thread = std::move(recv_thread_);
        if (thread) {
            thread->Exit();
        }
        state_.reset();
    }

    void PxRunningPipe::ReceiveLoop(const std::shared_ptr<State>& state) {
        CHandle pipe;
        pipe.Attach(CreateNamedPipeA(
            state->pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1024, 1024, NMPWAIT_USE_DEFAULT_WAIT, nullptr));
        if (static_cast<HANDLE>(pipe) == INVALID_HANDLE_VALUE) {
            pipe.Detach();
            LOGE("Create pipe {} failed, error={}", state->pipe_name, GetLastError());
            return;
        }

        while (!state->stopping.load(std::memory_order_acquire)) {
            if (!ConnectClient(pipe, state->stopping)) {
                break;
            }
            std::string signal;
            const bool read = ReadSignal(pipe, state->stopping, signal);
            DisconnectNamedPipe(pipe);
            if (!read) {
                if (!state->stopping.load(std::memory_order_acquire)) {
                    LOGI("Read from {} failed, error={}", state->pipe_name, GetLastError());
                }
                break;
            }
            LOGI("Read from {}: {}", state->pipe_name, signal);
            if (signal == kPipeSignal && state->callback) {
                state->callback();
            }
        }
    }

    bool PxRunningPipe::SendHello() {
        CHandle pipe;
        pipe.Attach(CreateFileA(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (static_cast<HANDLE>(pipe) == INVALID_HANDLE_VALUE) {
            pipe.Detach();
            return false;
        }

        DWORD written = 0;
        if (!WriteFile(pipe, kPipeSignal.data(), static_cast<DWORD>(kPipeSignal.size()),
                       &written, nullptr)) {
            LOGE("Write to {} failed, error={}", pipe_name_, GetLastError());
            return false;
        }
        return written == kPipeSignal.size();
    }
}
