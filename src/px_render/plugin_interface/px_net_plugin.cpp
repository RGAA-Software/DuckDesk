//
// Created by RGAA on 21/11/2024.
//

#include "px_net_plugin.h"
#include "px_plugin_events.h"

namespace px
{

    PxNetPlugin::PxNetPlugin() {
        plugin_type_ = PxPluginType::kNet;
    }

    PxNetPlugin::~PxNetPlugin() {

    }

    void PxNetPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {

    }

    bool PxNetPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    bool PxNetPlugin::PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    void PxNetPlugin::PostUserProxyMessage(std::shared_ptr<Data> msg) {
    }

    bool PxNetPlugin::IsUserProxyConnected() {
        return false;
    }

    void PxNetPlugin::PostIpcBinaryMessage(std::shared_ptr<Data> msg) {
    }

    void PxNetPlugin::RegisterIpcPid(uint32_t pid) {
    }

    void PxNetPlugin::OnClientEventCame(bool is_proto,
                                        int64_t socket_fd,
                                        const NetPluginType& nt_plugin_type,
                                        const NetChannelType& ch_type,
                                        std::shared_ptr<Data> msg) {
        auto event = std::make_shared<PxPluginNetClientEvent>();
        event->is_proto_ = is_proto;
        event->socket_fd_ = socket_fd;
        event->nt_plugin_type_ = nt_plugin_type;
        event->nt_channel_type_ = ch_type;
        event->message_ = msg;
        event->from_plugin_ = this;
        CallbackEvent(event);
    }

    bool PxNetPlugin::IsOnlyAudioClients() {
        return false;
    }

    int PxNetPlugin::GetConnectedClientsCount() {
        return 0;
    }

    int PxNetPlugin::GetMediaConsumersCount() {
        return GetConnectedClientsCount();
    }

    void PxNetPlugin::SyncInfo(const NetSyncInfo& info) {
        sync_info_ = info;
    }

    int64_t PxNetPlugin::GetQueuingMediaMsgCount() {
        return 0;
    }

    int64_t PxNetPlugin::GetQueuingFtMsgCount() {
        return 0;
    }

    bool PxNetPlugin::HasEnoughBufferForQueuingMediaMessages() {
        return false;
    }

    bool PxNetPlugin::HasEnoughBufferForQueuingFtMessages() {
        return false;
    }

    void PxNetPlugin::ReportSentDataSize(int size) {
        auto event = std::make_shared<PxPluginDataSent>();
        event->size_ = size;
        CallbackEvent(event);
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> PxNetPlugin::GetConnectedClientInfo() {
        return {};
    }

}
