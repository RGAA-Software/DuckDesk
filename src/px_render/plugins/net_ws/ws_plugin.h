//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_WS_PLUGIN_H
#define GAMMARAY_WS_PLUGIN_H

#include <mutex>
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px
{

    class WsPluginServer;

    class WsPlugin : public GrNetPlugin {
    public:
        WsPlugin();
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::GrPluginParam& param) override;
        bool OnDestroy() override;
        void On1Second() override;
        bool IsWorking() override;

        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetFileTransferProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;

        void PostUserProxyMessage(std::shared_ptr<Data> msg) override;
        bool IsUserProxyConnected() override;
        void PostIpcBinaryMessage(std::shared_ptr<Data> msg) override;
        void RegisterIpcPid(uint32_t pid) override;

        bool IsOnlyAudioClients() override;
        int GetConnectedClientsCount() override;

        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        std::vector<std::shared_ptr<GrConnectedClientInfo>> GetConnectedClientInfo() override;
        void DispatchAppEvent(const std::shared_ptr<AppBaseEvent> &event) override;
        void OnMessageAck(const std::shared_ptr<NetMessageAck> &ack) override;
        GrNetPlugin* GetLocalRtcPlugin();

        // 记录当前编码输出的显示器名(Web 端输入回放需要 monitor_name)
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const GrPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key) override;
        std::string GetCapturingMonitorName();

    private:
        bool HasConnectedClients();

    private:
        std::shared_ptr<WsPluginServer> ws_server_ = nullptr;
        std::shared_ptr<NetMessageAck> last_ack_ = nullptr;
        std::mutex capturing_mon_mtx_;
        std::string capturing_mon_name_;
        // exe 侧通过插件参数下发("app_mode");DLL 内的 RdSettings 单例是独立副本不可用
        bool game_hook_mode_ = false;
    };

}


#endif //GAMMARAY_UDP_PLUGIN_H
