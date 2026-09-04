//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_VR_MANAGER_PLUGIN_H
#define PX_VR_MANAGER_PLUGIN_H

#include "px_render/plugin_interface/px_net_plugin.h"

namespace px
{

    class RelayPluginRuntime;

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
        std::atomic<std::shared_ptr<RelayPluginRuntime>> runtime_;
    };

}



#endif //PX_UDP_PLUGIN_H
