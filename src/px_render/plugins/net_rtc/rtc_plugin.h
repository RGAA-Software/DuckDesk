//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_RTC_PLUGIN_H
#define GAMMARAY_RTC_PLUGIN_H

#include "px_render/plugin_interface/px_net_plugin.h"
#include "rtc_messages.h"
#include "px_common_new/concurrent_hashmap.h"

namespace px
{

    class RtcServer;

    class RtcPlugin : public GrNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::GrPluginParam &param) override;
        void OnMessage(std::shared_ptr<Message> msg) override;
        void OnMessageRaw(const std::any &msg) override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        int GetConnectedClientsCount() override;
        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;

    private:
        void OnRemoteSdp(const MsgRtcRemoteSdp& m);
        void OnRemoteIce(const MsgRtcRemoteIce& m);
        void WaitForMediaChannelActive();

    private:
        ConcurrentHashMap<std::string, std::shared_ptr<RtcServer>> rtc_servers_;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
