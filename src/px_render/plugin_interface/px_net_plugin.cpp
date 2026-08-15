//
// Created by RGAA on 21/11/2024.
//

#include "px_net_plugin.h"
#include "px_plugin_events.h"

namespace px
{

    GrNetPlugin::GrNetPlugin() {
        plugin_type_ = GrPluginType::kNet;
    }

    GrNetPlugin::~GrNetPlugin() {

    }

    void GrNetPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {

    }

    bool GrNetPlugin::PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    bool GrNetPlugin::PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) {
        return false;
    }

    void GrNetPlugin::PostUserProxyMessage(std::shared_ptr<Data> msg) {
    }

    bool GrNetPlugin::IsUserProxyConnected() {
        return false;
    }

    void GrNetPlugin::PostIpcBinaryMessage(std::shared_ptr<Data> msg) {
    }

    void GrNetPlugin::RegisterIpcPid(uint32_t pid) {
    }

    void GrNetPlugin::OnClientEventCame(bool is_proto,
                                        int64_t socket_fd,
                                        const NetPluginType& nt_plugin_type,
                                        const NetChannelType& ch_type,
                                        std::shared_ptr<Data> msg) {
        auto event = std::make_shared<GrPluginNetClientEvent>();
        event->is_proto_ = is_proto;
        event->socket_fd_ = socket_fd;
        event->nt_plugin_type_ = nt_plugin_type;
        event->nt_channel_type_ = ch_type;
        event->message_ = msg;
        event->from_plugin_ = this;
        CallbackEvent(event);
    }

    bool GrNetPlugin::IsOnlyAudioClients() {
        return false;
    }

    int GrNetPlugin::GetConnectedClientsCount() {
        return 0;
    }

    void GrNetPlugin::SyncInfo(const NetSyncInfo& info) {
        sync_info_ = info;
    }

    int64_t GrNetPlugin::GetQueuingMediaMsgCount() {
        return 0;
    }

    int64_t GrNetPlugin::GetQueuingFtMsgCount() {
        return 0;
    }

    bool GrNetPlugin::HasEnoughBufferForQueuingMediaMessages() {
        return false;
    }

    bool GrNetPlugin::HasEnoughBufferForQueuingFtMessages() {
        return false;
    }

    void GrNetPlugin::ReportSentDataSize(int size) {
        auto event = std::make_shared<GrPluginDataSent>();
        event->size_ = size;
        CallbackEvent(event);
    }

    std::vector<std::shared_ptr<GrConnectedClientInfo>> GrNetPlugin::GetConnectedClientInfo() {
        return {};
    }

}
