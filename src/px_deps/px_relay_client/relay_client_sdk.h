//
// Created by RGAA on 28/02/2025.
//

#ifndef PX_RELAY_CLIENT_SDK_H
#define PX_RELAY_CLIENT_SDK_H

#include <memory>
#include <functional>
#include "relay_callbacks.h"
#include "relay_client_sdk_param.h"
#include "px_common/concurrent_hashmap.h"

namespace px_relay
{
    class RelayMessage;
}

namespace px
{

    class Data;
    class RelayRoom;
    class RelayContext;
    class RelayNetClient;
    class PxAsyncRuntime;
    class FileTransferWritableSignal;

    // Only connect to one remote
    class RelayClientSdk : public std::enable_shared_from_this<RelayClientSdk> {
    public:
        explicit RelayClientSdk(const RelayClientSdkParam& param);
        RelayClientSdk(const RelayClientSdkParam& param,
                       std::shared_ptr<PxAsyncRuntime> async_runtime);
        RelayClientSdk(const RelayClientSdkParam& param,
                       std::shared_ptr<RelayNetClient> net_client);
        void Start();
        void Stop();
        void SetOnRelayServerConnectedCallback(OnRelayServerConnected&& cbk);
        void SetOnRelayServerDisConnectedCallback(OnRelayServerDisConnected&& cbk);
        void SetOnRelayRoomPreparedCallback(OnRelayRoomPrepared&& cbk);
        void SetOnRelayRoomDestroyedCallback(OnRelayRoomDestroyed&& cbk);
        void SetOnRelayProtoMessageCallback(std::function<void(const std::shared_ptr<px_relay::RelayMessage>&)>&& cbk);
        void SetOnRelayErrorCallback(OnRelayError&& cbk);
        void SetOnRelayRemoteDeviceOffline(OnRelayRemoteDeviceOffline&& cbk);
        void RelayProtoMessage(std::shared_ptr<Data> msg);

        std::shared_ptr<RelayNetClient> GetNetClient();
        int64_t GetQueuingMsgCount();
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal();

        // send from client
        void RequestCreateRoom();
        // received from server
        void OnCreatedRoomResp(const std::shared_ptr<px_relay::RelayMessage>& msg);

        // send from client
        void RequestControl();
        // received from server
        void OnRequestControlResp(const std::shared_ptr<px_relay::RelayMessage>& msg);

        // send from client
        void RequestStopRelay();
        // received from server
        void OnRequestStopRelayResp(const std::shared_ptr<px_relay::RelayMessage>& msg);

        // received from server
        void OnErrorMessage(const std::shared_ptr<px_relay::RelayMessage>& msg);

        // has relay rooms or not
        bool IsInRoom();

        // request pause stream
        void RequestPauseStream();

        // request resume stream
        void RequestResumeStream();

        // Retry...
        void RetryConnection();

    private:
        void PostBinMessage(const std::string& msg);
        std::shared_ptr<px_relay::RelayMessage> ProcessProtoMessage(std::shared_ptr<Data> msg);

        void OnRoomPrepared(const std::shared_ptr<px_relay::RelayMessage>& msg);
        void OnRoomInfoChanged(const std::shared_ptr<px_relay::RelayMessage>& msg);
        void OnRoomDestroyed(const std::shared_ptr<px_relay::RelayMessage>& msg);
        void OnRemoteDeviceOffline(const std::shared_ptr<px_relay::RelayMessage>& msg);

    private:
        std::shared_ptr<RelayContext> context_ = nullptr;
        RelayClientSdkParam sdk_param_;
        std::shared_ptr<RelayNetClient> ws_client_ = nullptr;
        std::shared_ptr<RelayRoom> room_ = nullptr;
        std::atomic_int64_t relay_msg_index_ = 0;
        std::mutex relay_mtx_;
        OnRelayRoomPrepared cbk_room_prepared_;
        OnRelayRoomDestroyed cbk_room_destroyed_;
        OnRelayError  cbk_relay_error_;
        OnRelayRemoteDeviceOffline cbk_remote_device_offline_;
    };
}

#endif //PX_RELAY_CLIENT_SDK_H
