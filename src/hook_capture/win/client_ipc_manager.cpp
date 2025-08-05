//
// Created by RGAA on 2023/12/23.
//

#include "client_ipc_manager.h"

#include "tc_common_new/ipc_shm.h"
#include "tc_common_new/ipc_msg_queue.h"
#include "tc_common_new/log.h"
#include "tc_common_new/data.h"
#include "tc_common_new/time_util.h"
#include "capture_message.h"

#include <thread>

namespace tc
{

    std::shared_ptr<ClientIpcManager> ClientIpcManager::Make() {
        return std::make_shared<ClientIpcManager>();
    }

    ClientIpcManager::ClientIpcManager() = default;

    void ClientIpcManager::Init(uint32_t pid, uint32_t shm_buffer_size) {
        this->pid_ = pid;
        this->shm_buffer_size_ = shm_buffer_size;
        auto ipc_shm_client_to_host_name = "ipc_shm_client_to_host_" + std::to_string(this->pid_);

        auto ipc_event_host_to_client_name = "ipc_event_host_to_client_" + std::to_string(this->pid_);
        auto ipc_event_client_to_host_name = "ipc_event_client_to_host_" + std::to_string(this->pid_);

        auto mtx_host_to_client_name = "mtx_host_to_client_" + std::to_string(this->pid_);
        auto mtx_client_to_host_name = "mtx_client_to_host_" + std::to_string(this->pid_);

//        host_to_client_event_ = std::make_shared<Poco::NamedEvent>(ipc_event_host_to_client_name);
//        client_to_host_event_ = std::make_shared<Poco::NamedEvent>(ipc_event_client_to_host_name);
//
//        client_to_host_shm_ = std::make_shared<Poco::SharedMemory>(ipc_shm_client_to_host_name, shm_buffer_size, Poco::SharedMemory::AccessMode::AM_WRITE);
//        LOGI("Shm buffer size: {}", shm_buffer_size);
//
//        host_to_client_mtx_ = std::make_shared<Poco::NamedMutex>(mtx_host_to_client_name);
//        client_to_host_mtx_ = std::make_shared<Poco::NamedMutex>(mtx_client_to_host_name);
    }

    static uint64_t last_send_time = TimeUtil::GetCurrentTimestamp();

    void ClientIpcManager::Send(const char* data, int size) {
        std::lock_guard<std::mutex> guard(shm_send_mtx_);
//
//        if (size > shm_buffer_size_) {
//            return;
//        }
//
//        auto current_time = TimeUtil::GetCurrentTimestamp();
//        auto diff = current_time - last_send_time;
//        last_send_time = current_time;
//        //LOGI("diff: {}", diff);
//
//        client_to_host_mtx_->lock();
//        auto begin = client_to_host_shm_->begin();
//        auto header = FixHeader {
//            .buffer_length = static_cast<uint32_t>(size),
//            .buffer_index = buffer_index_++,
//            .buffer_timestamp = TimeUtil::GetCurrentTimePointUS(),
//        };
//        memcpy(begin, (char*)&header, sizeof(FixHeader));
//        memcpy(begin + sizeof(FixHeader), data, size);
//        client_to_host_mtx_->unlock();
//
//        client_to_host_event_->set();
    }

    void ClientIpcManager::Send(const std::shared_ptr<Data>& data) {
        this->Send(data->CStr(), data->Size());
    }

    void ClientIpcManager::Send(std::shared_ptr<Data>&& data) {
        this->Send(data->CStr(), data->Size());
    }

    void ClientIpcManager::Send(const std::string &data) {
        this->Send(data.c_str(), data.size());
    }

    void ClientIpcManager::MockSend() {
        std::thread t([this]() {
            std::ostringstream ss;
            ss << "Good...";
            for (int i = 0; i < 100000000; i++) {
                //std::string data = "current index = " + std::to_string(i) + ", ok.";
//                ss.clear();
//                ss << "current index : " << i << ", ok.";
                CaptureAudioFrame capture_audio_frame_msg{};
                capture_audio_frame_msg.frame_index_ = i;
                auto data = Data::Make(nullptr, sizeof(CaptureAudioFrame));
                memcpy(data->DataAddr(), &capture_audio_frame_msg, sizeof(CaptureAudioFrame));
                this->Send(data);
                std::this_thread::sleep_for(std::chrono::milliseconds(17));

//                auto current_time = TimeUtil::GetCurrentTimestamp();
//                auto diff = current_time - last_send_time;
//                last_send_time = current_time;
//                LOGI("diff: {}", diff);
            }
        });
        t.detach();
    }

    void ClientIpcManager::WaitForMessage() {
//        recv_thread_ = std::make_shared<std::thread>([=, this] () {
//            while(!exit_) {
//                host_to_client_event_->wait();
//                if (!host_to_client_shm_) {
//                    auto ipc_shm_host_to_client_name = "ipc_shm_host_to_client_" + std::to_string(pid_);
//                    host_to_client_shm_ = std::make_shared<Poco::SharedMemory>(ipc_shm_host_to_client_name, kHostToClientShmSize, Poco::SharedMemory::AccessMode::AM_READ);
//                }
//                host_to_client_mtx_->lock();
//                auto begin = host_to_client_shm_->begin();
//                auto header = (FixHeader*)begin;
//                auto buffer = Data::Make(begin + sizeof(FixHeader), (int)header->buffer_length);
//                host_to_client_mtx_->unlock();
//                auto parsed_message = this->ParseMessage(std::move(buffer));
//
//                auto in_msg = std::get<0>(parsed_message);
//                auto in_data = std::get<1>(parsed_message);
//                if (!in_msg) {
//                    LOGE("Error message in .");
//                    continue;
//                }
//
//                if (in_msg->type_ == kCaptureHelloMessage && ipc_hello_msg_callback_) {
//                    ipc_hello_msg_callback_(std::move(std::static_pointer_cast<CaptureHelloMessage>(in_msg)));
//                }
//                else if (in_msg && ipc_msg_callback_) {
//                    ipc_msg_callback_(in_msg, in_data);
//                }
//            }
//        });
    }

    void ClientIpcManager::Exit() {
        exit_ = true;
        if (recv_thread_ && recv_thread_->joinable()) {
            recv_thread_->join();
        }
    }

    std::tuple<std::shared_ptr<CaptureBaseMessage>, std::shared_ptr<Data>> ClientIpcManager::ParseMessage(std::shared_ptr<Data>&& data) {
        auto msg = (CaptureBaseMessage*)data->CStr();

        std::shared_ptr<CaptureBaseMessage> cpy_msg = nullptr;
        std::shared_ptr<Data> cpy_data = nullptr;
        if (msg->type_ == kCaptureHelloMessage) {
            auto in_msg = AsMessage<CaptureHelloMessage>(msg);
            cpy_msg = std::dynamic_pointer_cast<CaptureBaseMessage>(in_msg);
            LOGI("====> CaptureHelloMessage in : {}, data length: {} ", std::static_pointer_cast<CaptureHelloMessage>(cpy_msg)->type_, cpy_msg->data_length);
        }
        else if (msg->type_ == kMouseEventMessage) {
            auto in_msg = AsMessage<MouseEventMessage>(msg);
            cpy_msg = std::dynamic_pointer_cast<CaptureBaseMessage>(in_msg);
        }
        else if (msg->type_ == kKeyboardEventMessage) {
            auto in_msg = AsMessage<KeyboardEventMessage>(msg);
            cpy_msg = std::dynamic_pointer_cast<KeyboardEventMessage>(in_msg);
        }
//        if (cpy_msg->data_length > 0 && cpy_msg->data_length < data->Size()) {
//            cpy_data = Data::Make(data + sizeof(CaptureVideoFrame), (int) cpy_msg->data_length);
//        }
        return {cpy_msg, cpy_data};
    }

    void ClientIpcManager::RegisterHelloMessageCallback(IpcHelloMessageCallback&& cbk) {
        ipc_hello_msg_callback_ = cbk;
    }

    void ClientIpcManager::RegisterIpcMessageCallback(IpcMessageCallback&& cbk) {
        ipc_msg_callback_ = cbk;
    }
}