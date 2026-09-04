//
// Created by RGAA on 1/03/2025.
//

#ifndef PX_RELAY_SERVER_SDK_H
#define PX_RELAY_SERVER_SDK_H

#include <memory>
#include <functional>
#include <atomic>
#include "relay_callbacks.h"
#include "relay_server_sdk_param.h"
#include "px_common_new/concurrent_hashmap.h"

namespace px_relay
{
    class RelayMessage;
}

namespace px
{
    class Data;
    class RelayRoom;
    class RelayContext;
    class RelayWsClient;
    class RelayConnectedClientInfo;
    class FileTransferWritableSignal;

    class RelayServerSdk : public std::enable_shared_from_this<RelayServerSdk> {
    public:
        explicit RelayServerSdk(const RelayServerSdkParam& param);
        void Start();
        void Stop();
        void SetOnConnectedCallback(OnRelayServerConnected&& cbk);
        void SetOnDisConnectedCallback(OnRelayServerDisConnected && cbk);
        void SetOnRelayHelloCallback(OnRelayServerHello&& cbk);
        void SetOnRelayHeartbeatCallback(OnRelayServerHeartbeat&& cbk);
        void SetOnRelayProtoMessageCallback(std::function<void(const std::shared_ptr<px_relay::RelayMessage>&)>&& cbk);
        void SetOnRoomPreparedCallback(OnRelayRoomPrepared&& cbk);
        void SetOnRoomDestroyedCallback(OnRelayRoomDestroyed&& cbk);
        void SetOnRequestPauseStreamCallback(OnRelayRequestPausedStream&& cbk);
        void SetOnRequestResumeStreamCallback(OnRelayRequestResumeStream&& cbk);
        void SetOnNotificationCallback(OnRelayNotification&& cbk);
        void SetOnRequestControlCallback(OnRelayRequestControl&& cbk);

        void RelayProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg);

        bool IsAlive();
        [[nodiscard]] std::uint64_t ConnectionGeneration() const;
        int64_t GetQueuingMsgCount();
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal();
        bool HasRelayRooms();
        std::shared_ptr<RelayRoom> GetRoomById(const std::string& room_id);
        int GetConnectedClientsCount();
        std::vector<std::shared_ptr<RelayConnectedClientInfo>> GetConnectedClientInfo();

    private:
        void PostBinMessage(const std::string& msg);
        std::shared_ptr<px_relay::RelayMessage> ProcessProtoMessage(std::shared_ptr<Data> msg);
        // request to control this device
        void OnRequestControl(const std::shared_ptr<px_relay::RelayMessage>& msg);

        void OnRoomPrepared(const std::shared_ptr<px_relay::RelayMessage>& msg);
        void OnRoomInfoChanged(const std::shared_ptr<px_relay::RelayMessage>& msg);
        void OnRoomDestroyed(const std::shared_ptr<px_relay::RelayMessage>& msg);

    private:
        std::shared_ptr<RelayContext> context_ = nullptr;
        RelayServerSdkParam sdk_param_;
        std::shared_ptr<RelayWsClient> ws_client_ = nullptr;
        px::ConcurrentHashMap<std::string, std::shared_ptr<RelayRoom>> rooms_;
        OnRelayServerHello hello_cbk_;
        OnRelayServerHeartbeat heartbeat_cbk_;
        OnRelayRoomPrepared room_prepared_cbk_;
        OnRelayRoomDestroyed room_destroyed_cbk_;
        OnRelayRequestPausedStream pause_stream_cbk_;
        OnRelayRequestResumeStream resume_stream_cbk_;
        OnRelayNotification notification_cbk_;
        OnRelayRequestControl req_control_cbk_;
        std::atomic_int64_t relay_msg_index_ = 0;
        std::atomic_bool connected_ = false;
        std::mutex relay_mtx_;
        // last timestamp(ms) a heartbeat/hello response was received from server,
        // used to detect gaps in the relay-alive reply chain (panel indicators)
        std::atomic_uint64_t last_alive_resp_ts_ = 0;
    };

}


#endif //PX_RELAY_SERVER_SDK_H
