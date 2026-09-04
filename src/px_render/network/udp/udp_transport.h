//
// Created by RGAA on 15/11/2024.
// Rewritten on 12/08/2026: GameStream 风格裸 UDP 媒体面(旧 KCP + proto 广播全部废弃),
// 设计见 docs/udp_gamestream_channel_plan.md,协议见 px_common_new/px_udp_protocol.h
//

#ifndef PX_UDP_TRANSPORT_H
#define PX_UDP_TRANSPORT_H

#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <Windows.h>
#include <asio2/udp/udp_server.hpp>
#include "architecture/modules/render_module.h"
#include "px_render/network/transport_types.h"
#include "px_common_new/concurrent_hashmap.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace px
{
    class Data;
    class PxAsyncRuntime;
    class UdpRuntimeState;

    struct UdpWinHandleCloser final {
        void operator()(void* handle) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): Win32 HANDLE boundary
    };

    using UdpWinHandle = std::unique_ptr<void, UdpWinHandleCloser>;

    // 一条远端 endpoint(addr:port)对应一条 UDP 会话;
    // 收到 kCtrlHello 绑定后才算媒体会话,之后才会向它发视频 shard
    class UdpSession {
    public:
        std::string conn_id_;           // remote addr:port
        std::string stream_id_;         // kCtrlHello 上报
        std::string association_code_;  // WS-created UDP media association
        std::shared_ptr<asio2::udp_session> sess_ = nullptr;
        // 绑定状态:变迁在 UdpRuntimeState 的 mutex 下做,读用原子(编码线程热路径无锁)
        std::atomic_bool bound_{false};
        // 被新连接按 stream_id 互踢的旧会话,禁止再通过 heartbeat 抢回绑定
        std::atomic_bool kicked_{false};
        int64_t begin_timestamp_ = 0;   // 绑定成功时间(ms),算断开 duration 用
        std::atomic<int64_t> last_heartbeat_ms_{0};
        // 最近一次收到该 endpoint 任意 UDP 包的时间;未绑定/被踢会话据此超时摘除
        std::atomic<int64_t> last_seen_ms_{0};
    };

    class UdpTransport final : public RenderModule {
    public:
        explicit UdpTransport(std::shared_ptr<PxAsyncRuntime> async_runtime = {});
        std::string Id() const override;
        std::string Name() const override;
        std::string VersionName() const override;
        uint32_t VersionCode() const override;
        std::string Description() const override;
        RenderModuleKind Kind() const override { return RenderModuleKind::kNetwork; }

        bool Start(const RenderModuleConfiguration& configuration) override;
        bool Destroy() override;
        void UpdateUdpMediaAssociation(
            const UdpMediaAssociation& association);
        // 视频走 OnEncodedVideoFrame 裸 UDP 直发;音频从这里提取 kAudioFrame 的
        // Opus payload 发 UDP(wire 级手扫,不引 protobuf 头);控制消息走 ws 通道
        void Broadcast(std::shared_ptr<Data> message, bool run_through = false);
        bool SendToStream(const std::string& stream_id, std::shared_ptr<Data> message, bool run_through = false);
        int ConnectedClientCount() const;
        bool HasOnlyAudioClients() const noexcept;
        bool IsWorking() const override;

        bool HasMediaCapacity() const noexcept;
        bool HasFileTransferCapacity() const noexcept;

        // data: encode video frame, h264/h265/...(编码线程回调,逐帧分包直发)
        void SubmitEncodedVideo(const std::string& mon_name,
                                 const PxPluginEncodedVideoType& video_type,
                                 const std::shared_ptr<Data>& data,
                                 uint64_t frame_index,
                                 int frame_width,
                                 int frame_height,
                                 bool key);

    private:
        // mon_name -> 单调递增 slot(u8),插件生命周期内保持稳定
        uint8_t MonSlotOf(const std::string& mon_name);
        // Sunshine 同款高精度 sleep:CreateWaitableTimerEx(HIGH_RESOLUTION) + SetWaitableTimer。
        void PaceSleep(const std::chrono::steady_clock::duration& duration);

    private:
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<UdpRuntimeState> runtime_{};
        int udp_listen_port_{};
        // 视频 shard MTU:LAN 默认 1400;公网/UDP 分片敏感场景可配 1024。
        int udp_mtu_{1400};
        // key = conn_id(remote addr:port):裸 UDP 下所有会话共享同一 socket,
        // native_handle 无法区分对端,必须用 endpoint 字符串做 key
        std::mutex mon_slot_mtx_;
        std::map<std::string, uint8_t> mon_slots_;
        uint8_t next_mon_slot_ = 0;

        // Sunshine 同款 pacing(stream.cpp),但速率上限按百兆网而非 1Gbps:
        // ratecontrol_packets_in_1ms = 100Mbps*80%/1000/blocksize/8 = 10000/blocksize。
        // 单批上限 64KB / 64 包,避开 Windows 64KB SO_SNDBUF 绕过问题。
        static constexpr uint64_t kRateControlBitsPerSec = 80000000ULL;  // 80 Mbps
        UdpWinHandle pace_timer_;
        // 跨帧锚定的速率控制起点(Sunshine ratecontrol_next_frame_start)
        std::chrono::steady_clock::time_point ratecontrol_next_frame_start_{};

        // 音频包序号(PostProtoMessage 由 rd_app 单线程调用,无需原子);
        // 50pps 小包,不走帧内 pacing
        uint32_t audio_seq_ = 0;
    };

}




#endif  // PX_UDP_TRANSPORT_H
