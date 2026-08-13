//
// Created by RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面(旧 KCP + proto 广播全部废弃),
// 设计见 docs/udp_gamestream_channel_plan.md,协议见 tc_common_new/gr_udp_protocol.h
//

#ifndef GAMMARAY_UDP_PLUGIN_H
#define GAMMARAY_UDP_PLUGIN_H

#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <Windows.h>
#include <asio2/udp/udp_server.hpp>
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "tc_common_new/concurrent_hashmap.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace tc
{
    class Data;

    // 一条远端 endpoint(addr:port)对应一条 UDP 会话;
    // 收到 kCtrlHello 绑定后才算媒体会话,之后才会向它发视频 shard
    class UdpSession {
    public:
        std::string conn_id_;           // remote addr:port
        std::string device_id_;         // kCtrlHello 上报
        std::string stream_id_;         // kCtrlHello 上报
        std::shared_ptr<asio2::udp_session> sess_ = nullptr;
        // 绑定状态:变迁在 UdpPlugin::bind_mtx_ 下做,读用原子(编码线程热路径无锁)
        std::atomic_bool bound_{false};
        // 被新连接按 stream_id 互踢的旧会话,禁止再通过 heartbeat 抢回绑定
        std::atomic_bool kicked_{false};
        int64_t begin_timestamp_ = 0;   // 绑定成功时间(ms),算断开 duration 用
        std::atomic<int64_t> last_heartbeat_ms_{0};
        // 最近一次收到该 endpoint 任意 UDP 包的时间;未绑定/被踢会话据此超时摘除
        std::atomic<int64_t> last_seen_ms_{0};
    };

    class UdpPlugin : public GrNetPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;

        bool OnCreate(const tc::GrPluginParam &param) override;
        bool OnDestroy() override;
        // 视频走 OnEncodedVideoFrame 裸 UDP 直发;音频从这里提取 kAudioFrame 的
        // Opus payload 发 UDP(wire 级手扫,不引 protobuf 头);控制消息走 ws 通道
        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through = false) override;
        bool PostTargetStreamProtoMessage(const std::string& stream_id, std::shared_ptr<Data> msg, bool run_through = false) override;
        int GetConnectedClientsCount() override;
        bool IsOnlyAudioClients() override;
        bool IsWorking() override;

        bool HasEnoughBufferForQueuingMediaMessages() override;
        bool HasEnoughBufferForQueuingFtMessages() override;

        // data: encode video frame, h264/h265/...(编码线程回调,逐帧分包直发)
        void OnEncodedVideoFrame(const std::string& mon_name,
                                 const GrPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key) override;

    private:
        void StartInternal();
        // 收包分流:ParseCommon 过一道,只处理 kPktCtrl(上行媒体 P2 才有)
        void HandleCtrlPacket(const std::shared_ptr<UdpSession>& udp_sess, const char* data, size_t size);
        // kCtrlHello:绑定会话(device_id/stream_id),同 stream_id 旧绑定会话互踢
        void HandleHello(const std::shared_ptr<UdpSession>& udp_sess,
                         const std::string& device_id, const std::string& stream_id);
        // kCtrlHeartbeat:刷心跳;NAT 换端口重建会话时按 stream_id 把绑定迁到当前会话
        void HandleHeartbeat(const std::shared_ptr<UdpSession>& udp_sess, const std::string& stream_id);
        // kCtrlFrameStatus:聚合窗口统计(完成帧数/FEC 恢复块数)
        void HandleFrameStatus(uint32_t frame_index, uint16_t received, uint16_t lost);
        // 5s 窗口:按 loss_rate 动态调 fec_percent_,窗口结束清零计数
        void AdjustFecWindow();
        // 是否存在至少一个已绑定媒体会话。发送路径不依赖 bound_count_,避免重连
        // /互踢时计数与会话状态短暂不一致导致视频静默停发。
        bool HasBoundSession();
        // 每 2s 扫一次,超 10s 无心跳的绑定会话解绑并发断开事件
        void SweepDeadSessions();
        void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        // mon_name -> 单调递增 slot(u8),插件生命周期内保持稳定
        uint8_t MonSlotOf(const std::string& mon_name);
        // Sunshine 同款高精度 sleep:CreateWaitableTimerEx(HIGH_RESOLUTION) + SetWaitableTimer。
        void PaceSleep(const std::chrono::steady_clock::duration& duration);

    private:
        std::shared_ptr<asio2::udp_server> server_ = nullptr;
        int udp_listen_port_{};
        // 视频 shard MTU:LAN 默认 1400;公网/UDP 分片敏感场景可配 1024。
        int udp_mtu_{1400};
        // key = conn_id(remote addr:port):裸 UDP 下所有会话共享同一 socket,
        // native_handle 无法区分对端,必须用 endpoint 字符串做 key
        tc::ConcurrentHashMap<std::string, std::shared_ptr<UdpSession>> sessions_;
        // 绑定/解绑/互踢/迁移的互斥(心跳时间戳用原子,不走这把锁)
        std::mutex bind_mtx_;
        // 已绑定媒体会话数(bind_mtx_ 下维护,读无锁)
        std::atomic_int bound_count_{0};

        std::mutex mon_slot_mtx_;
        std::map<std::string, uint8_t> mon_slots_;
        uint8_t next_mon_slot_ = 0;

        // 视频 FEC (Reed-Solomon) 校验包百分比,0 = 关闭;OnCreate 从 fec-percent 读入。
        // 编码线程读、窗口定时器写,用原子;动态调整只会在 [configured, 60] 区间浮动
        // 标准默认值对齐 Sunshine/Moonlight:20%。
        // 遇到持续丢帧时动态上调,上限仍为 60%。
        std::atomic_int fec_percent_{20};
        // toml 读到的初始值,动态下调不跌破它
        int configured_fec_percent_ = 20;
        // FRAME_STATUS 窗口统计(udp io 线程累加,窗口定时器读取清零)。
        // 注意:客户端对完成帧(FEC 恢复的)和判丢帧都会发 FrameStatus,wire 上无法区分两者;
        // 判丢帧客户端会 1:1 补发 kCtrlIdrRequest,因此 lost_frames 用 IDR 请求数计,
        // FrameStatus 的 lost 字段全部计入 recovered_shards(判丢帧的缺失数会轻微 inflate,可接受)
        std::atomic_int stat_complete_frames_{0};
        std::atomic_int stat_lost_frames_{0};
        std::atomic_int stat_recovered_shards_{0};
        // 诊断:窗口内实际入队的视频 shard 数与 async_send 短写次数
        std::atomic_uint64_t stat_sent_shards_{0};
        std::atomic_uint64_t stat_send_short_writes_{0};
        // 收到 RFI 请求后,给下一帧打 kFlagRfiRecover,让客户端能解参考帧失效后的 P 帧
        std::atomic_bool rfi_pending_{false};

        static constexpr int64_t kHeartbeatTimeoutMs = 10000;
        static constexpr int kHeartbeatScanIntervalMs = 2000;
        // 未绑定/被踢的底层 UDP 会话最多保留这么久,到期主动 stop 释放
        static constexpr int64_t kUnboundSessionTimeoutMs = 10000;
        static constexpr int kFecWindowMs = 5000;
        static constexpr int kFecMaxPercent = 60;
        // Sunshine 同款 pacing(stream.cpp),但速率上限按百兆网而非 1Gbps:
        // ratecontrol_packets_in_1ms = 100Mbps*80%/1000/blocksize/8 = 10000/blocksize。
        // 单批上限 64KB / 64 包,避开 Windows 64KB SO_SNDBUF 绕过问题。
        static constexpr uint64_t kRateControlBitsPerSec = 80000000ULL;  // 80 Mbps
        HANDLE pace_timer_ = nullptr;
        // 跨帧锚定的速率控制起点(Sunshine ratecontrol_next_frame_start)
        std::chrono::steady_clock::time_point ratecontrol_next_frame_start_{};

        // 音频包序号(PostProtoMessage 由 rd_app 单线程调用,无需原子);
        // 50pps 小包,不走帧内 pacing
        uint32_t audio_seq_ = 0;
    };

}


GR_PLUGIN_EXPORT(tc::UdpPlugin)


#endif //GAMMARAY_UDP_PLUGIN_H
