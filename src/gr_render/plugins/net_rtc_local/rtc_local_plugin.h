//
// Created by RGAA on 15/11/2024.
//

#ifndef GAMMARAY_MEDIA_RECORDER_PLUGIN_H
#define GAMMARAY_MEDIA_RECORDER_PLUGIN_H

#include <map>
#include <atomic>
#include <mutex>
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "tc_common_new/concurrent_hashmap.h"
#include "rtc_local_encoded_frame.h"
#include "tc_common_new/concurrent_type.h"

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
        GrLocalRtcAllocResult AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& info,
                                                       std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) override;

        // data: encode video frame, h264/h265/...
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const GrPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key) override;
        // raw video frame
        // handle: D3D Shared texture handle
        void OnRawVideoFrameSharedTexture(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) override;

        // raw video frame in rgba format
        // image: Raw image
        void OnRawVideoFrameRgba(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) override;

        // raw video frame in yuv(I420) format
        // image: Raw image
        void OnRawVideoFrameYuv(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) override;

        // 取主编码管线产出的编码帧:按 (mon_name, frame_index) 精确匹配,
        // 未命中退而取同屏最新缓存帧(见实现);mon_name 隔离多屏序号空间,
        // 切屏瞬间新旧屏同号帧不会互相覆盖/误取
        std::shared_ptr<RtcLocalEncodedVideoFrame> PopEncodedVideoFrame(uint16_t frame_index, const std::string& mon_name);
        void PrintCachedVideoFrames();
        void SetClearOlderFramesBaseline(int64_t baseline_timestamp);

        // RtcServer 在 ICE 终态(Failed/Closed)时回调,标记该连接待清理
        void NotifyRtcServerTerminal(const std::string& conn_id);

    private:
        void WaitForMediaChannelActive();
        // 定期清扫已终止的 RtcServer,防止死连接残留拖垮媒体投递
        void SweepDeadRtcServers();
        static std::string AddCandidateIpToAnswer(const std::string& ip, const std::string& answer);

    private:
        tc::ConcurrentHashMap<std::string, std::shared_ptr<RtcServer>> rtc_servers_;
        // encoded_video_frames_ 会被编码回调线程(OnEncodedVideoFrame)和
        // webrtc 编码线程(PopEncodedVideoFrame)并发访问,必须加锁。
        // key = mon_name + "#" + frame_index(各屏采集序号独立,必须带屏名区分)
        std::mutex encoded_video_frames_mtx_;
        std::map<std::string, std::shared_ptr<RtcLocalEncodedVideoFrame>> encoded_video_frames_;
        int64_t clear_baseline_timestamp_ = 0;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
