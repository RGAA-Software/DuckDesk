//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include <map>
#include "plugin_interface/gr_net_plugin.h"
#include "tc_common_new/concurrent_hashmap.h"

namespace tc
{
    class RtcServer;

    class RtcLocalPlugin : public GrNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const tc::GrPluginParam& param) override;
        void On1Second() override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        int GetConnectedClientsCount() override;
        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        bool AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& info,
                                      std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) override;

    private:
        void WaitForMediaChannelActive();
        std::string AddCandidateIpToAnwser(const std::string& ip, const std::string& anwser);

    private:
        tc::ConcurrentHashMap<std::string, std::shared_ptr<RtcServer>> rtc_servers_;
    };

}

extern "C" __declspec(dllexport) void* GetInstance();


#endif //GAMMARAY_UDP_PLUGIN_H
