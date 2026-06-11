//
// Created by RGAA on 2023/12/23.
//

#ifndef TC_APPLICATION_CLIENT_IPC_MANAGER_H
#define TC_APPLICATION_CLIENT_IPC_MANAGER_H

#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <functional>

#include "tc_capture_new/capture_message.h"

namespace tc
{

    constexpr auto kHostToClientShmSize = 2 * 1024 * 1024;

    struct FixHeader {
        uint32_t buffer_length = 0;
        uint32_t buffer_index = 0;
        uint64_t buffer_timestamp = 0;
        // data below
    };

    class Data;

    using IpcHelloMessageCallback = std::function<void(std::shared_ptr<CaptureHelloMessage>&&)>;
    using IpcMessageCallback = std::function<void(const std::shared_ptr<CaptureBaseMessage>&, std::shared_ptr<Data>&)>;

    class ClientIpcManager {
    public:

        // for easy-hook
        static ClientIpcManager* Instance() {
            static ClientIpcManager mgr;
            return &mgr;
        }

        // for obs hook
        static std::shared_ptr<ClientIpcManager> Make();

        ClientIpcManager();

        void Init(uint32_t pid, uint32_t shm_buffer_size);
        void Send(const std::string& data);
        void Send(const std::shared_ptr<Data>& data);
        void Send(std::shared_ptr<Data>&& data);
        void Send(const char* data, int size);
        void WaitForMessage();
        void Exit();

        void RegisterHelloMessageCallback(IpcHelloMessageCallback&& cbk);
        void RegisterIpcMessageCallback(IpcMessageCallback&& cbk);

        template<typename T>
        std::shared_ptr<T> AsMessage(CaptureBaseMessage* msg) {
            auto cpy_msg = std::make_shared<T>();
            memcpy(cpy_msg.get(), msg, sizeof(T));
            return cpy_msg;
        }

        void MockSend();

    private:
        std::tuple<std::shared_ptr<CaptureBaseMessage>, std::shared_ptr<Data>> ParseMessage(std::shared_ptr<Data>&& data);

    private:
        uint32_t pid_;
        bool exit_ = false;
        uint32_t buffer_index_ = 0;
        uint32_t shm_buffer_size_ = 0;
        std::shared_ptr<std::thread> recv_thread_ = nullptr;

//        std::shared_ptr<Poco::NamedEvent> client_to_host_event_ = nullptr;
//        std::shared_ptr<Poco::SharedMemory> client_to_host_shm_ = nullptr;
//        std::shared_ptr<Poco::NamedMutex> client_to_host_mtx_ = nullptr;
//
//        std::shared_ptr<Poco::NamedEvent> host_to_client_event_ = nullptr;
//        std::shared_ptr<Poco::SharedMemory> host_to_client_shm_ = nullptr;
//        std::shared_ptr<Poco::NamedMutex> host_to_client_mtx_ = nullptr;

        std::mutex shm_send_mtx_;

        IpcHelloMessageCallback ipc_hello_msg_callback_ = nullptr;
        IpcMessageCallback ipc_msg_callback_ = nullptr;

    };

}

#endif //TC_APPLICATION_CLIENT_IPC_MANAGER_H
