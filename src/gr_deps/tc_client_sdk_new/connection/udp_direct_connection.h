//
// Created by RGAA on 12/08/2026.
//

#ifndef GAMMARAYPC_UDP_DIRECT_CONNECTION_H
#define GAMMARAYPC_UDP_DIRECT_CONNECTION_H

#include "connection.h"
#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <map>
#include "tc_common_new/gr_udp_protocol.h"

namespace asio2
{
    class udp_client;
}

namespace tc
{

    class Message;

    // GameStream 风格的裸 UDP 媒体通道(非 KCP),控制面仍走 ws,
    // 由 sdk_net_client.cpp 的 kUdpDirect 分支与 WsConnection 一起启动:
    // - 上行:hello(按源地址绑定媒体会话)/heartbeat/IDR 请求,均为 GrUdpProtocol 控制包
    // - 下行:视频 shard 经 GrUdpFrameReassembler 组帧后合成标准 kVideoFrame proto 上送,
    //   与 webrtc_local 的 encoded-sink 路径一致(不回 Ack);
    //   音频包经 GrUdpAudioJitterBuffer 按序交付,合成标准 kAudioFrame proto 上送,
    //   缺口合成空 data proto 通知解码层走 Opus PLC
    // - watchdog:10s 收不到任何 UDP 包视为媒体面断开,走正常断线回调
    class UdpDirectConnection : public Connection {
    public:
        UdpDirectConnection(const std::shared_ptr<ThunderSdkParams>& params,
                            const std::shared_ptr<MessageNotifier>& notifier);
        ~UdpDirectConnection() override = default;

        // host/port 为 render 的 UDP 媒体端口;device_id/stream_id 用于 hello 会话绑定
        void Start(const std::string& host, int udp_port,
                   const std::string& device_id, const std::string& stream_id);
        void Start() override {}
        void Stop() override;

        // 仅用于上行 UDP 控制包(hello/heartbeat/IDR),proto 媒体消息不走这里
        void PostBinaryMessage(std::shared_ptr<Data> msg) override;

        // 组帧完成后合成的 kVideoFrame proto,回调语义与 WebRtcLocalConnection::SetOnVideoMessageCallback 一致
        void SetOnVideoMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk);

        // jitter buffer 按序交付后合成的 kAudioFrame proto;
        // 丢帧信号同样是 kAudioFrame,但 data 为空(解码层据此走 Opus PLC 补 20ms)
        void SetOnAudioMessageCallback(const std::function<void(std::shared_ptr<tc::Message>)>& cbk);

        // render 通过 UDP 控制包踢人(kCtrlKick),reason 原样上报
        void SetOnKickCallback(std::function<void(const std::string& reason)> cbk);

        bool IsAlive() override;

    private:
        void OnUdpPacket(const char* data, size_t size);
        void OnCompleteFrame(const GrUdpFrameReassembler::CompleteFrame& frame);
        void RequestIdr(const std::string& mon_name);
        void RequestIdrKeepalive(const std::string& mon_name);
        void RequestRfi(uint64_t invalid_frame_index, const std::string& mon_name);
        void CheckNeedIdr();
        void CheckWatchdog();

    private:
        static constexpr int kTimerHeartbeat = 1;
        static constexpr int kTimerWatchdog = 2;
        static constexpr int kTimerIdrRetry = 3;
        static constexpr int64_t kWatchdogTimeoutMs = 10000;

        std::string host_;
        int udp_port_ = 0;
        std::string device_id_;
        std::string stream_id_;

        std::shared_ptr<asio2::udp_client> udp_client_ = nullptr;
        GrUdpFrameReassembler reassembler_;
        GrUdpAudioJitterBuffer audio_jitter_;

        std::function<void(std::shared_ptr<tc::Message>)> video_msg_cbk_;
        std::function<void(std::shared_ptr<tc::Message>)> audio_msg_cbk_;
        std::function<void(const std::string& reason)> on_kick_cbk_;

        std::atomic_bool connected_ = false;
        std::atomic_bool stopped_ = false;
        std::atomic_bool disconn_reported_ = false;
        std::atomic_int64_t last_recv_ms_ = 0;
        std::atomic_int64_t last_video_frame_ms_{0};
        std::atomic_int64_t last_idr_request_ms_{0};
        // 最近一次 RFI 请求时间(ms)。目前仅用于日志/诊断,恢复靠 RFI 重试而非 300ms 转 IDR。
        std::atomic_int64_t last_rfi_request_ms_{0};
        std::atomic_uint64_t recv_pkt_count_{0};
        std::atomic_uint64_t recv_video_pkt_count_{0};
        std::atomic_uint64_t malformed_video_pkt_count_{0};

        // IDR 请求节流:per mon_slot 上次发 IDR 的时间(仅 udp io 线程访问,无需锁)
        std::map<uint8_t, std::chrono::steady_clock::time_point> last_idr_time_;
        std::map<uint8_t, std::chrono::steady_clock::time_point> last_rfi_time_;
        static constexpr int64_t kIdrThrottleMs = 1000;
        // RFI 不节流(Moonlight 同款):每次判丢立即发 RFI,靠 render 的 range 失效覆盖连续丢帧;
        // 恢复帧再丢也会被下一次判丢触发重发。0 = 关闭节流。
        static constexpr int64_t kRfiThrottleMs = 0;
        static constexpr int64_t kNoFrameTimeoutMs = 2000;

        // 音频判丢日志节流计数(仅 udp io 线程访问):正常交付时清零
        int audio_lost_log_count_ = 0;
    };

}

#endif //GAMMARAYPC_UDP_DIRECT_CONNECTION_H
