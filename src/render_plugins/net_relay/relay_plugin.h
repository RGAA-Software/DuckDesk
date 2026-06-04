//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_VR_MANAGER_PLUGIN_H
#define GAMMARAY_VR_MANAGER_PLUGIN_H

#include "plugin_interface/gr_net_plugin.h"

namespace tc
{

    class RelayServerSdk;

    class RelayPlugin : public GrNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;
        bool OnCreate(const tc::GrPluginParam &param) override;
        bool OnDestroy() override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        int GetConnectedClientsCount() override;
        bool IsOnlyAudioClients() override;
        bool IsWorking() override;
        void SyncInfo(const tc::NetSyncInfo& info) override;
        void OnSyncPluginSettingsInfo(const tc::GrPluginSettingsInfo &settings) override;
        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        std::vector<std::shared_ptr<GrConnectedClientInfo>> GetConnectedClientInfo() override;
        void OnMessageAck(const std::shared_ptr<NetMessageAck> &ack) override;

    private:
        void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id);
        void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        void ReportRelayAlive(const std::string& device_id);

    private:
        std::shared_ptr<RelayServerSdk> relay_media_sdk_ = nullptr;
        std::shared_ptr<RelayServerSdk> relay_ft_sdk_ = nullptr;
        std::atomic_uint64_t recv_relay_ft_msg_index_ = 0;
        std::atomic_uint64_t recv_relay_media_msg_index_ = 0;

        // don't send media stream at begin
        // client will request to resume it
        bool paused_stream = true;

        std::shared_ptr<NetMessageAck> last_ack_ = nullptr;

        std::string using_appkey_;
        std::atomic_bool need_reconnect_ = false;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
