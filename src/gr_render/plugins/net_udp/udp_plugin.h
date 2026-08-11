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
#include <asio2/udp/udp_server.hpp>
#include "gr_render/plugin_interface/gr_net_plugin.h"
#include "tc_common_new/concurrent_hashmap.h"

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
        int64_t begin_timestamp_ = 0;   // 绑定成功时间(ms),算断开 duration 用
        std::atomic<int64_t> last_heartbeat_ms_{0};
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
        // 媒体帧走 OnEncodedVideoFrame 裸 UDP 直发,控制消息走 ws 通道,
        // 本插件不转发任何 proto 消息
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
        // 每 2s 扫一次,超 10s 无心跳的绑定会话解绑并发断开事件
        void SweepDeadSessions();
        void NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        void NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp);
        // mon_name -> 单调递增 slot(u8),插件生命周期内保持稳定
        uint8_t MonSlotOf(const std::string& mon_name);

    private:
        std::shared_ptr<asio2::udp_server> server_ = nullptr;
        int udp_listen_port_{};
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

        static constexpr int64_t kHeartbeatTimeoutMs = 10000;
        static constexpr int kHeartbeatScanIntervalMs = 2000;
    };

}


GR_PLUGIN_EXPORT(tc::UdpPlugin)


#endif //GAMMARAY_UDP_PLUGIN_H
