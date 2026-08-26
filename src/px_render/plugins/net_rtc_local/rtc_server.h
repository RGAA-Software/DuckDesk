//
// Created by hy on 2024/4/25.
//

#ifndef TEST_WEBRTC_RTCSERVER_H
#define TEST_WEBRTC_RTCSERVER_H

#include "px_common_new/webrtc_helper.h"
#include "px_common_new/rtc_monitor_track_slots.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include <algorithm>
#include <mutex>

namespace px
{
    class Data;
    class PeerCallback;
    class CreateSessCallback;
    class SetSessCallback;
    class RtcLocalPlugin;
    class RtcDataChannel;
    class AudioSourceImpl;
    class VideoSourceImpl;
    class VideoTrackSourceImpl;
    class RemoteAudioSink;
    enum class PxLocalRtcSessionRole;

    // 一路 video track 对应一台显示器:每条 track 只发自己那块屏的帧
    struct MonitorVideoTrack {
        std::string mon_name_;
        std::shared_ptr<VideoSourceImpl> source_;
        rtc::scoped_refptr<VideoTrackSourceImpl> track_source_;
        uint64_t last_frame_index_ = 0;
    };

    class RtcServer : public std::enable_shared_from_this<RtcServer> {
    public:
        static std::shared_ptr<RtcServer> Make(RtcLocalPlugin* plugin);
        explicit RtcServer(RtcLocalPlugin* plugin);
        RtcLocalPlugin* GetPlugin();

        bool Start(const std::string& stream_id, const std::string& offer_sdp,
                   PxLocalRtcSessionRole session_role,
                   const std::string& ice_config_json = "");
        bool RestartWithOffer(const std::string& offer_sdp,
                              const std::string& ice_config_json);
        void Exit();
        void OnRemoteIce(const std::string& ice, const std::string& mid, int sdp_mline_index);
        bool IsDataChannelConnected();
        bool IsFtDataChannelConnected();
        bool IsMediaConsumerActive() const;
        bool IsWallObserver() const { return wall_observer_; }

        // conn_id: rtc_servers_ 的 map key(device_id:stream_id),断开清理时回传给 plugin
        void SetConnId(const std::string& conn_id) { conn_id_ = conn_id; }
        const std::string& GetConnId() const { return conn_id_; }
        // 真实访客 stream id(信令传入,与 px::Message.stream_id 一致)。
        // 连接/断开事件都用它做 visitor 标识,保证按 id 键控的插件能配对。
        const std::string& GetStreamId() const { return stream_id_; }
        // client_nonce: web client 的浏览器标识(launch 页 nonce)。
        // 新连接 nonce 与现存活跃连接相同 = 同一浏览器,信令直接自动接管,
        // 不再回 704 让用户确认;不同 nonce 维持占用确认流程
        void SetClientNonce(const std::string& nonce) { client_nonce_ = nonce; }
        const std::string& GetClientNonce() const { return client_nonce_; }
        void SetPermissions(bool capability_enforced, const std::vector<std::string>& permissions) {
            capability_enforced_ = capability_enforced;
            permissions_ = permissions;
        }
        bool HasPermission(const std::string& permission) const {
            return !capability_enforced_
                || std::find(permissions_.begin(), permissions_.end(), permission) != permissions_.end();
        }
        // 请求退出:置 exit_ 标记,停止一切收发;真正的资源回收由 plugin 延迟 Sweep
        void RequestExit() { exit_ = true; }
        bool IsExitRequested() const { return exit_.load(); }

        // 插件级客户端断开事件:ICE 瞬断/终态、媒体 datachannel 独立关闭、ICE 超时
        // 判死,任一检测点触发,全连接生命周期只发一次(去重)。
        // stream_id 用真实访客 stream id(与 px::Message.stream_id 一致),
        // 不用 datachannel 内部 the_conn_id_(MD5)——否则按消息 stream_id 键控的
        // 插件(ft 文件传输、joystick)永远匹配不上断开事件。
        void EmitClientDisconnectedEvent();

        void PostProtoMessage(std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);
        bool PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through = false);

        uint32_t GetMediaPendingMessages();
        uint32_t GetFtPendingMessages();

        bool HasEnoughBufferForQueuingMediaMessages();
        bool HasEnoughBufferForQueuingFtMessages();

        void On100msTimeout();

        std::string GetAnswerSdp();
        void SetOnAnswerCallback(std::function<void(const std::string& answer_sdp)>&& callback);

        void OnNewFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format);
        // CPU 采集(GDI/mock)裸帧通知,无纹理 handle
        void OnNewRawFrameCaptured(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height);

        // 信令用:本连接 video track 的显示器名列表(按 track 顺序)
        std::vector<std::string> GetVideoTrackMonitors() const;

        // Local loopback PCM → outbound WebRTC audio track (RTP).
        // samples = sample rate (Hz), matching OnRawAudioData convention.
        void OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits);
        bool SetVoiceCallAuthorization(const std::string& call_id, bool authorized);
        void OnVoiceCallPcm(
            const std::string& call_id, const int16_t* samples,
            size_t sample_count, int sample_rate, int channels);

    private:
        void CreatePeerConnectionFactory();
        void CreatePeerConnection();
        bool ApplyIceConfiguration(const std::string& ice_config_json,
                                   bool update_peer_connection);
        bool SetRemoteOffer(const std::string& offer_sdp);
        // 按屏路由 + 构造 NotifyFrameFrameBuffer 推给 video source(OnNewFrameCaptured/
        // OnNewRawFrameCaptured 的公共尾部)
        void DispatchCapturedFrameNotify(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format);
        void CreateSomeMediaDeps(webrtc::PeerConnectionFactoryDependencies& media_deps);

        void SendIceToRemote(const std::string& ice, const std::string& mid, int sdp_mline_index);

        // 远端音频轨(浏览器麦克风上行):挂 PCM sink,经 WASAPI 播放
        void OnRemoteAudioTrack(rtc::scoped_refptr<webrtc::AudioTrackInterface> track);
        void OnRemoteAudioTrackRemoved(rtc::scoped_refptr<webrtc::AudioTrackInterface> track);

    private:
        RtcLocalPlugin* plugin_ = nullptr;
        std::unique_ptr<rtc::Thread> network_thread_;
        std::unique_ptr<rtc::Thread> worker_thread_;
        std::unique_ptr<rtc::Thread> sig_thread_;
        std::string stream_id_;
        std::string offer_sdp_;
        std::string ice_config_json_;
        bool standard_rtc_ = false;
        std::string answer_sdp_;
        std::shared_ptr<PeerCallback> peer_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_remote_offer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<SetSessCallback> set_local_answer_sdp_callback_ = nullptr;
        rtc::scoped_refptr<CreateSessCallback> create_answer_callback_ = nullptr;

        rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_conn_ = nullptr;
        rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_conn_factory_;
        // Kept alive for factory lifetime; dummy ADM avoids mic-capture race.
        rtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
        webrtc::PeerConnectionInterface::RTCConfiguration configuration_;

        std::shared_ptr<RtcDataChannel> media_data_channel_ = nullptr;
        std::shared_ptr<RtcDataChannel> ft_data_channel_ = nullptr;
        // 输入专用通道(web client 以 unreliable/unordered 创建,避免可靠通道
        // 丢包时的队头阻塞造成操作不跟手);消息与 media 通道同路由进输入回放
        std::shared_ptr<RtcDataChannel> input_data_channel_ = nullptr;
        // 诊断 ping 通道:收到即原样回显,web 端实测 datachannel RTT
        std::shared_ptr<RtcDataChannel> ping_data_channel_ = nullptr;
        std::atomic<bool> exit_ = false;
        // Exit 幂等标记:ICE 终态回调/插件 Sweep/takeover 替换都可能触发 Exit
        std::atomic<bool> cleaned_up_ = false;
        std::string conn_id_;
        std::string client_nonce_;
        std::vector<std::string> permissions_;
        bool capability_enforced_ = false;
        bool wall_observer_ = false;
        std::atomic_bool ice_connected_ = false;
        // The initial observer timeout only applies before the first successful
        // ICE connection. Once connected, transient disconnects use the normal
        // reconnect grace period instead of being mistaken for offer timeout.
        std::atomic_bool ice_ever_connected_ = false;
        int64_t created_timestamp_ms_ = 0;
        std::function<void(const std::string& answer_sdp)> answer_sdp_callback_;
        // ICE 进入 Disconnected 的起始时间(0 = 未处于 Disconnected)。
        // 网络/对端异常断开时 ICE 可能长期停在 Disconnected 而不进 Failed/Closed,
        // On100msTimeout 据此超时判死并请求退出,避免死连接拖垮媒体投递。
        std::atomic<int64_t> ice_disconnected_since_ms_{0};
        // ICE can briefly report Disconnected while the same data channel is
        // still recoverable. Confirm it for one second before emitting the
        // final client-disconnected event; RdApplication then applies the
        // user-facing five-second no-client grace period.
        // ICE 的 Disconnected 可能只是网卡切换/短暂丢包。给连接 5 秒恢复窗口，
        // 与媒体消费者退出后的采集保活窗口一致，避免监看墙和普通客户端闪断。
        static constexpr int64_t kIceDisconnectedTimeoutMs = 5000;
        // A browser may abandon an offer before ICE connects. Hidden wall
        // sessions have no data channel whose close event could reclaim them,
        // so expire incomplete observers explicitly instead of leaking one of
        // the bounded observer slots forever.
        static constexpr int64_t kWallObserverConnectTimeoutMs = 15000;
        // 断开事件去重:见 EmitClientDisconnectedEvent
        std::atomic_bool disconnect_event_sent_ = false;

        // 视频轨布局:offer 只有 1 条 video m-line(web/旧客户端)时为 false,
        // 保持单动态 track 旧行为(跟随切屏);>=2 条(新 Windows 客户端)时为 true。
        // 多轨模式按 offer 预留固定槽位,显示器热插拔只改变槽位映射,无需 SDP 重协商。
        bool multi_track_mode_ = false;
        mutable std::mutex video_tracks_mutex_;
        std::vector<MonitorVideoTrack> video_tracks_;
        rtc::scoped_refptr<AudioSourceImpl> audio_source_ = nullptr;
        rtc::scoped_refptr<AudioSourceImpl> voice_audio_source_ = nullptr;
        std::string authorized_voice_call_id_;
        mutable std::mutex voice_mutex_;

        // 上行音频(浏览器麦克风):统计 sink(dummy ADM 下不做自动外放)
        rtc::scoped_refptr<webrtc::AudioTrackInterface> remote_audio_track_ = nullptr;
        std::shared_ptr<RemoteAudioSink> remote_audio_sink_ = nullptr;
    };

}

#endif //TEST_WEBRTC_RTCSERVER_H
