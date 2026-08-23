//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_RTC_LOCAL_PLUGIN_H
#define PX_RENDER_RTC_LOCAL_PLUGIN_H

#include <map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_capture_new/monitor_util.h"
#include "px_common_new/concurrent_hashmap.h"
#include "rtc_local_encoded_frame.h"
#include "px_common_new/concurrent_type.h"

namespace px
{
    class RtcServer;

    class RtcLocalPlugin : public PxNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;
        void On1Second() override;
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) override;
        int GetConnectedClientsCount() override;
        int GetMediaConsumersCount() override;
        int64_t GetQueuingMediaMsgCount() override;
        int64_t GetQueuingFtMsgCount() override;
        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;
        PxLocalRtcAllocResult AllocNewLocalRtcInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& info,
                                                       std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& callback) override;

        // data: encode video frame, h264/h265/...
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const PxPluginEncodedVideoType& video_type,
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
        bool SetVoiceCallAuthorization(
            const std::string& stream_id, const std::string& call_id,
            bool authorized) override;
        void OnVoiceCallPcm(
            const std::string& stream_id, const std::string& call_id,
            const int16_t* samples, size_t sample_count,
            int sample_rate, int channels) override;
        void OnRemoteVoiceCallPcm(
            const std::string& stream_id, const std::string& call_id,
            const int16_t* samples, size_t sample_count,
            int sample_rate, int channels);

        // 按编码产出序号非破坏性读取:每个 WebRTC 编码器持有独立 cursor,多连接
        // 可读取同一帧。生产端有界缓存负责淘汰旧帧;out_gap 表示该消费者落后并
        // 丢失了 delta 链,消费端应 InsertIdr 等待关键帧续接。
        std::shared_ptr<RtcLocalEncodedVideoFrame> ReadNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq, bool& out_gap);
        // 有界等待该屏 seq > after_seq 的编码帧到达(Encode 首次 Pop 为空时调用),
        // 等到返回 true。用于把"采集帧驱动的 Encode 只能搬上一帧"的固有 1.5 帧
        // 拾取延迟压缩到编码管线延迟以内;超时返回 false,调用方按现状空转返回。
        bool WaitForEncodedFrame(const std::string& mon_name, uint64_t after_seq, int timeout_ms);
        // 按屏补 IDR:mon_name 为空时广播(基类旧行为),非空只给目标屏编码器补
        void InsertIdr(const std::string& mon_name);
        // 暴露基类无参版本,避免上面的重载把 PxPluginInterface::InsertIdr() 隐藏掉
        using PxPluginInterface::InsertIdr;
        // 该屏当前最大产出序号(无则 0);新连接/切屏后以此引导,只消费之后到达的帧
        uint64_t GetLatestEncodedSeq(const std::string& mon_name);
        // 该屏缓存中 seq > after_seq 的未消费帧数(积压监控/应急阀用)
        size_t GetCachedFrameCount(const std::string& mon_name, uint64_t after_seq);
        void PrintCachedVideoFrames();

        // RtcServer 在 ICE 终态(Failed/Closed)时回调,标记该连接待清理
        // conn_id 只是 map key;重连时同一个 key 会被新 RtcServer 复用,
        // 必须同时比对 RtcServer 指针,避免旧连接的迟到回调把新连接误杀。
        void NotifyRtcServerTerminal(const std::string& conn_id, RtcServer* target);

        // 本机显示器列表(枚举顺序,上限 kMaxRtcVideoTracks),供 RtcServer 建多 track
        // 及信令返回 monitors 列表;空表示采集插件未就绪(回退单 track 旧行为)
        std::vector<CaptureMonitorInfo> GetRtcTrackMonitors();
        // 多 track 会话需要所有屏的帧:让工作中的采集插件采集全部显示器
        // (客户端 offer 多条 video m-line 即声明要多屏,不再依赖 UI 的 SwitchMonitor)
        void EnableAllMonitorCapture();
        static constexpr int kMaxRtcVideoTracks = 4;

    private:
        void WaitForMediaChannelActive();
        // 定期清扫已终止的 RtcServer,防止死连接残留拖垮媒体投递
        void SweepDeadRtcServers();
        static std::string AddCandidateIpToAnswer(const std::string& ip, const std::string& answer);

    private:
        px::ConcurrentHashMap<std::string, std::shared_ptr<RtcServer>> rtc_servers_;
        // encoded_video_frames_ 会被编码回调线程(OnEncodedVideoFrame)和
        // webrtc 编码线程(ReadNextEncodedVideoFrame)并发访问,必须加锁。
        // key = (mon_name, seq):按屏隔离 + 按产出序号数值有序
        std::mutex encoded_video_frames_mtx_;
        std::map<std::pair<std::string, uint64_t>, std::shared_ptr<RtcLocalEncodedVideoFrame>> encoded_video_frames_;
        // 新编码帧到达时唤醒 WaitForEncodedFrame(与上面的 mutex 配对使用)
        std::condition_variable encoded_video_frames_cv_;
        // 每屏产出序号计数器(只在 OnEncodedVideoFrame 加锁自增)
        std::map<std::string, uint64_t> encoded_seq_by_mon_;
        // 每屏缓存上限(编码快于消费时淘汰最旧帧,消费端会发现 gap 并 InsertIdr)
        static constexpr size_t kMaxCachedFramesPerMon = 8;
        // All RTC peers share one physical encoder per monitor. Coalesce their
        // PLI/chain-recovery requests globally instead of throttling only
        // inside each peer-local RtcSharedVideoEncoder.
        std::mutex idr_request_mtx_;
        std::map<std::string, std::chrono::steady_clock::time_point> last_idr_request_by_mon_;
        static constexpr int64_t kMinIdrRequestIntervalMs = 800;
        // 最近一次 shared-texture 采集帧时间戳:非 0 且很新说明 DDA 纹理路径在工作,
        // OnRawVideoFrameYuv 的裸帧喂 webrtc 要抑制(DDA+CPU 编码回退时两者都会到,
        // 重复喂会让 webrtc Encode 双倍消费 seq,断链)。纯 GDI/mock 时没有
        // shared-texture 事件,由 YUV 裸帧路径驱动 webrtc。
        std::atomic<int64_t> last_shared_tex_ts_{0};
        // libwebrtc 的 SSL 环境是进程级资源。RTC Local 允许多个会话并存后，
        // 不能再由单个 RtcServer 的退出去清理，否则会破坏其余在线连接。
        bool ssl_initialized_ = false;
    };

}



#endif //PX_UDP_PLUGIN_H
