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

        // Local loopback PCM → each RtcServer outbound audio track (RTP).
        // samples = sample rate (Hz).
        void OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) override;

        // 按编码产出序号顺序取帧:返回 mon_name 屏 seq > after_seq 的最旧一帧(严格按
        // 编码器产出顺序消费,H264 delta 链不断裂;NVENC 跳帧编码时采集序号不连续,
        // 不能再按采集序号匹配)。out_gap: 有未消费的帧被淘汰(缓存溢出)时为 true,
        // 消费端应 InsertIdr 等关键帧续接。无新帧返回 nullptr。
        std::shared_ptr<RtcLocalEncodedVideoFrame> PopNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq, bool& out_gap);
        // 该屏当前最大产出序号(无则 0);新连接/切屏后以此引导,只消费之后到达的帧
        uint64_t GetLatestEncodedSeq(const std::string& mon_name);
        // 该屏缓存中 seq > after_seq 的未消费帧数(积压监控/应急阀用)
        size_t GetCachedFrameCount(const std::string& mon_name, uint64_t after_seq);
        void PrintCachedVideoFrames();

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
        // webrtc 编码线程(PopNextEncodedVideoFrame)并发访问,必须加锁。
        // key = (mon_name, seq):按屏隔离 + 按产出序号数值有序
        std::mutex encoded_video_frames_mtx_;
        std::map<std::pair<std::string, uint64_t>, std::shared_ptr<RtcLocalEncodedVideoFrame>> encoded_video_frames_;
        // 每屏产出序号计数器(只在 OnEncodedVideoFrame 加锁自增)
        std::map<std::string, uint64_t> encoded_seq_by_mon_;
        // 每屏缓存上限(编码快于消费时淘汰最旧帧,消费端会发现 gap 并 InsertIdr)
        static constexpr size_t kMaxCachedFramesPerMon = 90;
    };

}



#endif //GAMMARAY_UDP_PLUGIN_H
