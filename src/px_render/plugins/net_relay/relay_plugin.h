//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_VR_MANAGER_PLUGIN_H
#define PX_VR_MANAGER_PLUGIN_H

#include <mutex>
#include <unordered_map>
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px
{

    class RelayServerSdk;

    class RelayPlugin : public PxNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;
        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnDestroy() override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        FileTransferSendResult PostTargetFileTransferProtoMessage(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {}) override;
        int GetConnectedClientsCount() override;
        bool IsOnlyAudioClients() override;
        bool IsWorking() override;
        void SyncInfo(const px::NetSyncInfo& info) override;
        void OnSyncPluginSettingsInfo(const px::PxPluginSettingsInfo &settings) override;
        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        std::vector<std::shared_ptr<PxConnectedClientInfo>> GetConnectedClientInfo() override;
        void OnMessageAck(const std::shared_ptr<NetMessageAck> &ack) override;

    private:
        void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id);
        void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        void ReportRelayAlive(const std::string& device_id);

        // relay_media_sdk_/relay_ft_sdk_ are reassigned by the relay monitor thread
        // while statistics/encoder/sdk-callback threads read them, so every access
        // must copy the shared_ptr under this lock
        std::shared_ptr<RelayServerSdk> GetMediaSdk();
        std::shared_ptr<RelayServerSdk> GetFtSdk();
        void SetMediaSdk(const std::shared_ptr<RelayServerSdk>& sdk);
        void SetFtSdk(const std::shared_ptr<RelayServerSdk>& sdk);

    private:
        struct FtRelayRouteInfo {
            std::string stream_id;
            std::string visitor_device_id;
            std::string connection_instance_id;
            int64_t created_timestamp = 0;
            uint64_t last_recv_msg_index = 0;
            bool has_recv_msg_index = false;
        };

        std::mutex sdks_mtx_;
        std::shared_ptr<RelayServerSdk> relay_media_sdk_ = nullptr;
        std::shared_ptr<RelayServerSdk> relay_ft_sdk_ = nullptr;
        std::atomic_uint64_t recv_relay_media_msg_index_ = 0;
        std::mutex ft_route_mtx_;
        std::unordered_map<std::string, FtRelayRouteInfo> ft_routes_;
        uint64_t ft_route_generation_ = 0;

        // don't send media stream at begin
        // client will request to resume it
        bool paused_stream = true;

        std::shared_ptr<NetMessageAck> last_ack_ = nullptr;

        std::string using_appkey_;
        std::string relay_device_id_;
        std::atomic_bool need_reconnect_ = false;
    };

}



#endif //PX_UDP_PLUGIN_H
