//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_RTC_PLUGIN_H
#define PX_RENDER_RTC_PLUGIN_H

#include "px_render/plugin_interface/px_net_plugin.h"
#include "rtc_messages.h"
#include "px_common_new/concurrent_hashmap.h"

namespace px
{

    class RtcServer;

    class RtcPlugin : public PxNetPlugin {
    public:
        RtcPlugin() = default;

        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnDestroy() override;
        void OnMessageRaw(const std::any &msg) override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        FileTransferSendResult PostTargetFileTransferProtoMessage(
            const std::string& stream_id,
            std::shared_ptr<Data> msg,
            bool run_through,
            const std::string& connection_instance_id = {}) override;
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



#endif //PX_UDP_PLUGIN_H
