//
// Created RGAA on 15/11/2024.
//

#include "rtc_local_plugin.h"
#include "rtc_server.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "px_common_new/log.h"
#include "px_common_new/file.h"
#include "px_common_new/image.h"
#include "px_common_new/time_util.h"
#include "px_common_new/data.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"

PX_PLUGIN_EXPORT(px::RtcLocalPlugin)

namespace px
{

    std::string RtcLocalPlugin::GetPluginId() {
        return kNetRtcLocalPluginId;
    }

    std::string RtcLocalPlugin::GetPluginName() {
        return "RTC Local";
    }

    std::string RtcLocalPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t RtcLocalPlugin::GetVersionCode() {
        return 110;
    }

    std::string RtcLocalPlugin::GetPluginDescription() {
        return "RTC in local mode";
    }

    void RtcLocalPlugin::On1Second() {
        PxPluginInterface::On1Second();
        SweepDeadRtcServers();
    }

    void RtcLocalPlugin::NotifyRtcServerTerminal(const std::string& conn_id, RtcServer* target) {
        LOGW("Rtc server terminal notified, conn_id: {}, will be swept.", conn_id);
        rtc_servers_.Apply(conn_id, [target](const std::shared_ptr<RtcServer>& srv) {
            if (srv.get() == target) {
                srv->RequestExit();
            }
        });
    }

    void RtcLocalPlugin::SweepDeadRtcServers() {
        std::vector<std::pair<std::string, std::shared_ptr<RtcServer>>> dead_servers;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            if (srv->IsExitRequested()) {
                dead_servers.emplace_back(k, srv);
            }
        });
        for (const auto& [k, srv] : dead_servers) {
            // 只删除扫描到的旧对象；同 key 已被新会话复用时保持新值不动。
            rtc_servers_.RemoveIf(k, [srv](const std::shared_ptr<RtcServer>& current) {
                return current == srv;
            });
            LOGI("Sweep dead rtc server: {}", k);
            srv->Exit();
        }
    }
    
    bool RtcLocalPlugin::OnCreate(const px::PxPluginParam &param) {
        PxPluginInterface::OnCreate(param);
        plugin_type_ = PxPluginType::kNet;

        if (!IsPluginEnabled()) {
            return true;
        }

        ssl_initialized_ = rtc::InitializeSSL();
        if (!ssl_initialized_) {
            LOGE("RTC Local failed to initialize SSL.");
            return false;
        }

        plugin_context_->StartTimer(100, [=, this]() {
            rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
                srv->On100msTimeout();
            });
        });

        return true;
    }

    bool RtcLocalPlugin::OnDestroy() {
        // 先进入 Stopping,阻止后续回调/事件继续向外投递;再逐路关闭 RtcServer。
        PxNetPlugin::OnStop();

        std::vector<std::shared_ptr<RtcServer>> servers;
        rtc_servers_.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
            if (srv) {
                servers.push_back(srv);
            }
        });
        rtc_servers_.Clear();

        for (const auto& srv : servers) {
            srv->Exit();
        }

        {
            std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
            encoded_video_frames_.clear();
            encoded_seq_by_mon_.clear();
        }

        if (ssl_initialized_) {
            rtc::CleanupSSL();
            ssl_initialized_ = false;
        }

        return PxNetPlugin::OnDestroy();
    }

    // 视频/音频帧消息不该走 datachannel:RTC 的音视频走 RTP 轨,web 端也不认识
    // 这类 proto 消息。但 app 会把每个编码帧广播给所有 net 插件
    // (plugin_stream_event_router.cpp VisitNetPlugins→PostProtoMessage),
    // 之前照单全收经 SCTP 转发 => ~9Mbps 的 20KB/帧 消息洪水:
    // render 每帧 PostTask+memcpy、WaitForMediaChannelActive 在帧分发线程自旋,
    // Chrome 主线程每秒 60 次 20KB TLV 重组+proto 解码后丢弃——
    // 主线程被淹正是 web 端"帧率低+完全不跟手"的根因(视频 RTP 轨本身健康)。
    // wire 级扫描 px.Message 的 type 字段(field 10, varint, tag=0x50),媒体帧直接丢弃。
    // 注意:type 不是 field 1;device_id/stream_id 可能在前,必须按 wire 格式逐字段跳过。
    static bool IsMediaFrameMessage(const std::shared_ptr<Data>& msg) {
        if (!msg || msg->Size() < 2) {
            return false;
        }
        const auto* p = (const uint8_t*)msg->DataAddr();
        const size_t n = msg->Size();
        size_t i = 0;
        auto read_varint = [&](uint64_t& out) -> bool {
            out = 0;
            int shift = 0;
            while (i < n && shift < 64) {
                uint8_t b = p[i++];
                out |= (uint64_t)(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    return true;
                }
                shift += 7;
            }
            return false;
        };
        // 逐字段扫描,找到 field 10(type)为止;负载按 wire type 跳过
        while (i < n) {
            uint64_t tag = 0;
            if (!read_varint(tag)) {
                return false;
            }
            const uint32_t field = (uint32_t)(tag >> 3);
            const uint32_t wire = (uint32_t)(tag & 0x7);
            if (field == 10 && wire == 0) {
                uint64_t type = 0;
                if (!read_varint(type)) {
                    return false;
                }
                // px_message.proto: kVideoFrame = 30, kAudioFrame = 40
                return type == 30 || type == 40;
            }
            switch (wire) {
            case 0: { uint64_t v; if (!read_varint(v)) { return false; } break; }
            case 1: i += 8; break;
            case 2: {
                uint64_t len = 0;
                if (!read_varint(len)) { return false; }
                i += (size_t)len;
                break;
            }
            case 5: i += 4; break;
            default: return false; // group 等不支持,视为非媒体帧
            }
            if (i > n) {
                return false;
            }
        }
        return false;
    }

    void RtcLocalPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        if (IsMediaFrameMessage(msg)) {
            return;
        }
        WaitForMediaChannelActive();

        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostProtoMessage(msg, run_through);
        });
    }

    bool RtcLocalPlugin::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        if (IsMediaFrameMessage(msg)) {
            return true;
        }
        WaitForMediaChannelActive();

        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostTargetStreamProtoMessage(stream_id, msg, run_through);
        });
        return true;
    }

    bool RtcLocalPlugin::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        auto queuing_msg_count = GetQueuingFtMsgCount();
        auto has_buffer = this->HasEnoughBufferForQueuingFtMessages();
        auto wait_count = 0;
        while ((queuing_msg_count > 256 || !has_buffer) && wait_count < 2000) {
            if (rtc_servers_.Empty()) {
                LOGW("===> Send file, no alive rtc server, drop the message.");
                return false;
            }
            TimeUtil::DelayBySleep(1);
            has_buffer = this->HasEnoughBufferForQueuingFtMessages();
            queuing_msg_count = GetQueuingFtMsgCount();
            wait_count++;
        }
        if (wait_count >= 2000) {
            // 拥塞日志限频:每 10s 最多一条,避免长时间拥塞时刷爆日志
            static std::atomic<int64_t> last_ft_timeout_log_ts = 0;
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            auto last = last_ft_timeout_log_ts.load();
            if (now - last >= 10000 && last_ft_timeout_log_ts.compare_exchange_strong(last, now)) {
                LOGW("===> Send file timeout after {}ms, drop the message, msg count: {}", wait_count, queuing_msg_count);
            }
            return false;
        }
        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostTargetFileTransferProtoMessage(stream_id, msg, run_through);
        });
        return true;
    }

    void RtcLocalPlugin::WaitForMediaChannelActive() {
        if (rtc_servers_.Empty()) {
            return;
        }
        // 媒体路径最多等 100ms:此函数运行在帧分发线程上,长时间自旋会堵死
        // 整个 render 消息循环。拥塞时直接放行投递(底层 channel 发送失败会自行丢弃)。
        static constexpr int kMaxWaitMs = 100;
        auto queuing_msg_count = GetQueuingMediaMsgCount();
        auto has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        auto wait_count = 0;
        while ((queuing_msg_count > 256 || !has_buffer) && wait_count < kMaxWaitMs) {
            if (rtc_servers_.Empty()) {
                LOGW("===> Send media, no alive rtc server, drop the message.");
                return;
            }
            TimeUtil::DelayBySleep(1);
            has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
            queuing_msg_count = GetQueuingMediaMsgCount();
            wait_count++;
        }
        // 拥塞日志限频:每 10s 最多一条,避免长时间拥塞时刷爆日志
        static std::atomic<int64_t> last_congestion_log_ts = 0;
        if (wait_count > 0) {
            auto now = (int64_t)TimeUtil::GetCurrentTimestamp();
            auto last = last_congestion_log_ts.load();
            if (now - last >= 10000 && last_congestion_log_ts.compare_exchange_strong(last, now)) {
                LOGW("===> Send media congested, wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
            }
        }
    }

    int RtcLocalPlugin::GetConnectedClientsCount() {
        int count = 0;
        rtc_servers_.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
            if (!srv->IsWallObserver() && srv->IsDataChannelConnected()) {
                count++;
            }
        });
        return count;
    }

    int RtcLocalPlugin::GetMediaConsumersCount() {
        int count = 0;
        rtc_servers_.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
            if (srv && srv->IsMediaConsumerActive()) {
                ++count;
            }
        });
        return count;
    }

    int64_t RtcLocalPlugin::GetQueuingMediaMsgCount() {
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            // 跳过死连接:其 pending 计数不会再变化,统计进来只会造成误判
            if (!srv->IsDataChannelConnected()) {
                return;
            }
            total_pending_messages += srv->GetMediaPendingMessages();
        });
        return total_pending_messages;
    }

    int64_t RtcLocalPlugin::GetQueuingFtMsgCount() {
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            // 跳过死连接:其 pending 计数不会再变化,统计进来只会造成误判
            if (!srv->IsFtDataChannelConnected()) {
                return;
            }
            total_pending_messages += srv->GetFtPendingMessages();
        });
        return total_pending_messages;
    }

    bool RtcLocalPlugin::HasEnoughBufferForQueuingMediaMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            // 跳过死连接:死连接的 channel 缓冲永远是满的,会全票否决所有投递
            if (!srv->IsDataChannelConnected()) {
                return;
            }
            flag &= srv->HasEnoughBufferForQueuingMediaMessages();
        });
        return flag;
    }

    bool RtcLocalPlugin::HasEnoughBufferForQueuingFtMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            // 跳过死连接:死连接的 channel 缓冲永远是满的,会全票否决所有投递
            if (!srv->IsFtDataChannelConnected()) {
                return;
            }
            flag &= srv->HasEnoughBufferForQueuingFtMessages();
        });
        return flag;
    }

    // data: encode video frame, h264/h265/...
    void RtcLocalPlugin::OnEncodedVideoFrame(const std::string& mon_name,
                             const PxPluginEncodedVideoType& video_type,
                             const std::shared_ptr<Data>& data,
                             uint64_t frame_index,
                             int frame_width,
                             int frame_height,
                             bool key) {
        // 诊断:确认编码帧是否到达本插件(每 300 帧打一条)
        static std::atomic_uint64_t encoded_frame_count = 0;
        auto ecnt = ++encoded_frame_count;
        {
            std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);

            auto encoded_video_frame = std::make_shared<RtcLocalEncodedVideoFrame>();
            encoded_video_frame->mon_name_ = mon_name;
            encoded_video_frame->video_type_ = (int)video_type;
            encoded_video_frame->data_ = data;
            encoded_video_frame->seq_ = ++encoded_seq_by_mon_[mon_name];
            encoded_video_frame->frame_index_ = frame_index;
            encoded_video_frame->frame_width_ = frame_width;
            encoded_video_frame->frame_height_ = frame_height;
            encoded_video_frame->key_ = key;
            encoded_video_frame->timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
            encoded_video_frames_.insert({{mon_name, encoded_video_frame->seq_}, encoded_video_frame});

            // 缓存上限:编码快于消费时淘汰该屏最旧帧(未消费就被淘汰的帧会让
            // 消费端发现 seq gap,进而 InsertIdr 等关键帧续接)
            auto first_of_mon = encoded_video_frames_.lower_bound({mon_name, 0});
            auto end_of_mon = encoded_video_frames_.lower_bound({mon_name, UINT64_MAX});
            size_t mon_count = 0;
            for (auto it = first_of_mon; it != end_of_mon; ++it) {
                (void)it;
                ++mon_count;
            }
            while (mon_count > kMaxCachedFramesPerMon && first_of_mon != encoded_video_frames_.end()
                   && first_of_mon->first.first == mon_name) {
                first_of_mon = encoded_video_frames_.erase(first_of_mon);
                --mon_count;
            }
            if (ecnt == 1 || ecnt % 300 == 0) {
                LOGI("OnEncodedVideoFrame #{}, idx={}, key={}, cache={}",
                     ecnt, frame_index, key, encoded_video_frames_.size());
            }
        }

        // 唤醒可能正在 WaitForEncodedFrame 的 webrtc 编码线程(锁外 notify)
        encoded_video_frames_cv_.notify_all();
    }

    // raw video frame
    // handle: D3D Shared texture handle
    void RtcLocalPlugin::OnRawVideoFrameSharedTexture(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {
        // 诊断:确认采集帧是否到达本插件(每 300 帧打一条)
        static std::atomic_uint64_t raw_frame_count = 0;
        auto cnt = ++raw_frame_count;
        if (cnt == 1 || cnt % 300 == 0) {
            LOGI("OnRawVideoFrameSharedTexture #{}, idx={}, {}x{}, handle={}, servers={}", cnt, frame_idx, frame_width, frame_height, handle, rtc_servers_.Size());
        }
        last_shared_tex_ts_ = TimeUtil::GetCurrentTimestamp();
        rtc_servers_.ApplyAll([=, this](const auto&, const std::shared_ptr<RtcServer>& rtc_server) {
            if (!rtc_server || rtc_server->IsExitRequested()) {
                return;
            }
            rtc_server->OnNewFrameCaptured(mon_name, frame_idx, frame_width, frame_height, handle, adapter_id, frame_format);
        });
    }

    // raw video frame in rgba format
    // image: Raw image
    void RtcLocalPlugin::OnRawVideoFrameRgba(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {

    }

    // raw video frame in yuv(I420) format
    // image: Raw image
    void RtcLocalPlugin::OnRawVideoFrameYuv(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, const std::shared_ptr<Image>& image) {
        // CPU 采集(GDI/mock)没有 shared-texture 事件,WebRTC 的视频源只能从这里
        // 拿到"有帧了"的通知——否则 webrtc 编码线程永远不调 Encode,预编码码流
        // 在缓存里积压到淘汰,客户端黑屏。DDA 路径(含 CPU 编码回退)已由
        // OnRawVideoFrameSharedTexture 喂过,这里抑制避免双倍消费 seq。
        if (TimeUtil::GetCurrentTimestamp() - last_shared_tex_ts_.load() < 1000) {
            return;
        }
        static std::atomic_uint64_t raw_yuv_count = 0;
        auto cnt = ++raw_yuv_count;
        if (cnt == 1 || cnt % 300 == 0) {
            LOGI("OnRawVideoFrameYuv notify webrtc #{}, idx={}, {}x{}, servers={}", cnt, frame_idx, frame_width, frame_height, rtc_servers_.Size());
        }
        rtc_servers_.ApplyAll([=, this](const auto&, const std::shared_ptr<RtcServer>& rtc_server) {
            if (!rtc_server || rtc_server->IsExitRequested()) {
                return;
            }
            rtc_server->OnNewRawFrameCaptured(mon_name, frame_idx, frame_width, frame_height);
        });
    }

    void RtcLocalPlugin::OnRawAudioData(const std::shared_ptr<Data>& data, int samples, int channels, int bits) {
        if (!data || data->Size() == 0) {
            return;
        }
        static std::atomic_uint64_t audio_cb_count = 0;
        auto cnt = ++audio_cb_count;
        if (cnt == 1 || cnt % 500 == 0) {
            LOGI("OnRawAudioData #{}, bytes={}, rate={} ch={} bits={}, peers={}",
                 cnt, data->Size(), samples, channels, bits, rtc_servers_.Size());
        }
        rtc_servers_.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
            if (!srv || srv->IsExitRequested() || srv->IsWallObserver()) {
                return;
            }
            srv->OnRawAudioData(data, samples, channels, bits);
        });
    }

    std::shared_ptr<RtcLocalEncodedVideoFrame> RtcLocalPlugin::ReadNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq, bool& out_gap) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        out_gap = false;
        // 该屏 seq 严格大于 after_seq 的最旧一帧(序消费,保证 H264 delta 链完整)
        auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
        if (it == encoded_video_frames_.end() || it->first.first != mon_name) {
            return nullptr;
        }
        // 最旧可取帧的 seq 跳号:中间有未消费的帧被缓存上限淘汰,delta 链已断
        out_gap = (it->first.second != after_seq + 1);
        // Non-destructive read: every RtcSharedVideoEncoder owns its own cursor.
        // The producer's bounded per-monitor cache evicts old frames, so one
        // slow observer cannot retain memory or steal frames from other peers.
        return it->second;
    }

    bool RtcLocalPlugin::WaitForEncodedFrame(const std::string& mon_name, uint64_t after_seq, int timeout_ms) {
        std::unique_lock<std::mutex> lk(encoded_video_frames_mtx_);
        return encoded_video_frames_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]() {
            auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
            return it != encoded_video_frames_.end() && it->first.first == mon_name;
        });
    }

    void RtcLocalPlugin::InsertIdr(const std::string& mon_name) {
        if (mon_name.empty()) {
            // 广播旧行为(新连接首帧 IDR 等需要所有屏的场景)
            PxPluginInterface::InsertIdr();
            return;
        }
        auto event = std::make_shared<PxPluginInsertIdrEvent>();
        event->mon_name_ = mon_name;
        CallbackEvent(event);
    }

    uint64_t RtcLocalPlugin::GetLatestEncodedSeq(const std::string& mon_name) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        auto it = encoded_seq_by_mon_.find(mon_name);
        return it != encoded_seq_by_mon_.end() ? it->second : 0;
    }

    size_t RtcLocalPlugin::GetCachedFrameCount(const std::string& mon_name, uint64_t after_seq) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        size_t count = 0;
        for (auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
             it != encoded_video_frames_.end() && it->first.first == mon_name; ++it) {
            ++count;
        }
        return count;
    }

    void RtcLocalPlugin::PrintCachedVideoFrames() {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        for (const auto& [key, frame] : encoded_video_frames_) {
            LOGI("=> mon: {}, seq: {}, key: {}", key.first, key.second, frame->key_);
        }
    }

    std::vector<CaptureMonitorInfo> RtcLocalPlugin::GetRtcTrackMonitors() {
        std::vector<CaptureMonitorInfo> result;
        // 插件没有直达 app 的通道,经 total_plugins_ 找工作中的采集插件(DDA 优先,GDI 兜底)
        for (const auto& plugin_id : { kDdaCapturePluginId, kGdiCapturePluginId }) {
            auto capture_plugin = dynamic_cast<PxMonitorCapturePlugin*>(GetPluginById(plugin_id));
            if (!capture_plugin) {
                continue;
            }
            result = capture_plugin->GetCaptureMonitorInfo();
            if (!result.empty()) {
                break;
            }
        }
        if ((int)result.size() > kMaxRtcVideoTracks) {
            result.resize(kMaxRtcVideoTracks);
        }
        return result;
    }

    void RtcLocalPlugin::EnableAllMonitorCapture() {
        // 与 GetRtcTrackMonitors 同一选取逻辑(DDA 优先,GDI 兜底)
        for (const auto& plugin_id : { kDdaCapturePluginId, kGdiCapturePluginId }) {
            auto capture_plugin = dynamic_cast<PxMonitorCapturePlugin*>(GetPluginById(plugin_id));
            if (!capture_plugin) {
                continue;
            }
            if (capture_plugin->GetCapturingMonitorName() != kAllMonitorsNameSign) {
                LOGI("Multi-track session: switch capture to ALL monitors.");
                capture_plugin->SetCaptureMonitor(kAllMonitorsNameSign);
            }
            break;
        }
    }

    PxLocalRtcAllocResult RtcLocalPlugin::AllocNewLocalRtcInstance(const std::shared_ptr<PxLocalRtcRequestInfo>& req,
                                                                   std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>&& callback) {
        auto conn_id = req->device_id_ + ":" + req->stream_id_;
        LOGI("==>AllocNewLocalRtcInstance Offer sdp {} => {}, takeover: {}", conn_id, req->sdp_.size(), req->takeover_);

        const bool is_observer = req->session_role_ == PxLocalRtcSessionRole::kWallObserver;
        static constexpr size_t kMaxWallObservers = 16;

        // Observer sessions coexist with the single interactive connection.
        // They use unique CMS-issued stream ids and never participate in the
        // takeover flow.
        std::vector<std::pair<std::string, std::shared_ptr<RtcServer>>> old_servers;
        size_t observer_count = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            if (!srv || srv->IsExitRequested()) {
                return;
            }
            if (srv->IsWallObserver()) {
                ++observer_count;
            }
            else {
                old_servers.emplace_back(k, srv);
            }
        });
        if (is_observer) {
            const bool duplicate = rtc_servers_.HasKey(conn_id);
            if (observer_count >= kMaxWallObservers || duplicate) {
                LOGW("Reject wall observer, count: {}, duplicate: {}", observer_count, duplicate);
                return PxLocalRtcAllocResult::kFailed;
            }
        }
        else if (!old_servers.empty()) {
            // 旧连接的 datachannel 仍活跃且调用方未确认接管:报告占用,由客户端决定
            // (web 弹确认后带 takeover=1 重试;原生客户端收到 704 会自动带 takeover 重试)
            if (!req->takeover_) {
                bool occupied = false;
                for (const auto& [k, srv] : old_servers) {
                    if (srv->IsDataChannelConnected()) {
                        // 同一浏览器(nonce 非空且相同)重复打开:自动接管旧连接,
                        // 不让用户点确认;nonce 为空(旧客户端/原生)或不同才报占用
                        if (!req->client_nonce_.empty() && req->client_nonce_ == srv->GetClientNonce()) {
                            LOGI("** Auto takeover: same client nonce, kick {}", k);
                            continue;
                        }
                        LOGW("** Occupied by an active connection: {}", k);
                        occupied = true;
                    }
                }
                if (occupied) {
                    return PxLocalRtcAllocResult::kOccupied;
                }
            }
            LOGI("** Remove {} old interactive connection(s).", old_servers.size());
            // 顶掉之前先通知旧客户端"连接被接管"(kConnectionTakenOver),
            // 让它给出明确提示并停止重连,而不是表现成一次普通断线。
            // px.Message{ type: kConnectionTakenOver(550) } 的 wire 字节:
            // type 是 field 10(varint, tag=0x50),550 的 varint 编码为 A6 04;
            // 两端客户端都只按 type 分发、子消息留空即可。不引 protobuf 头,
            // 本目标 webrtc 内置 absl 与 vcpkg absl 头文件会冲突(同
            // IsMediaFrameMessage 的 wire 级处理思路)。
            static const char kKickMsgBytes[] = { (char)0x50, (char)0xA6, (char)0x04 };
            auto kick_data = Data::Make(kKickMsgBytes, sizeof(kKickMsgBytes));
            for (const auto& [k, srv] : old_servers) {
                if (srv) {
                    srv->PostProtoMessage(kick_data, true);
                }
            }
            // 先从 map 整体摘除,让新连接立即可建;
            // Exit() 里 webrtc 线程 Stop() 可能因对端会话繁忙而长时间阻塞,
            // 绝不能跑在调用方线程(HTTP 信令线程)上,否则 takeover 请求会挂死信令
            for (const auto& [k, srv] : old_servers) {
                rtc_servers_.RemoveIf(k, [srv](const std::shared_ptr<RtcServer>& current) {
                    return current == srv;
                });
            }
            std::thread([old_servers = std::move(old_servers)]() {
                // 给"被接管"通知留出 SCTP 发送时间,再销毁旧连接
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                for (const auto& [k, srv] : old_servers) {
                    if (srv) {
                        srv->Exit();
                    }
                }
            }).detach();
        }

        auto rtc_server = RtcServer::Make(this);
        rtc_server->SetConnId(conn_id);
        rtc_server->SetClientNonce(req->client_nonce_);
        rtc_server->Start(req->stream_id_, req->sdp_, req->session_role_);
        rtc_server->SetOnAnswerCallback([=, this](const std::string& answer_sdp) {
            auto answer = rtc_server->GetAnswerSdp();
            auto new_answer = AddCandidateIpToAnswer(req->req_ip_, answer);
            auto reply = std::make_shared<PxLocalRtcReplyInfo>(PxLocalRtcReplyInfo {
                .answer_sdp_ = new_answer,
            });
            // 显示器列表(与 video track 同序),多 track 客户端据此做 track→mon_name 映射
            for (const auto& m : GetRtcTrackMonitors()) {
                reply->monitors_.push_back(PxLocalRtcMonitorInfo {
                    .name_ = m.name_,
                    .width_ = (int)m.Width(),
                    .height_ = (int)m.Height(),
                    .left_ = (int)m.left_,
                    .top_ = (int)m.top_,
                    .right_ = (int)m.right_,
                    .bottom_ = (int)m.bottom_,
                });
            }
            callback(reply);
        });
        rtc_servers_.Insert(conn_id, rtc_server);
        LOGI("Insert to map, will return information");

        return PxLocalRtcAllocResult::kOk;
    }

    std::string RtcLocalPlugin::AddCandidateIpToAnswer(const std::string& ip,const std::string& answer) {
        // std::unique_ptr<webrtc::SessionDescriptionInterface>
        auto session_desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer,answer);
        const webrtc::IceCandidateCollection* candidate_collection = session_desc->candidates(0);

        uint32_t max_priority = 0;
        for (int i = 0; i < candidate_collection->count(); ++i) {
            const webrtc::IceCandidateInterface* ice_candidate = candidate_collection->at(i);
            const cricket::Candidate& candidate = ice_candidate->candidate();
            if(candidate.priority() > max_priority) {
                max_priority = candidate.priority();
            }
            if(candidate.address().EqualIPs(rtc::SocketAddress(ip,0))) {
                LOGI("Found same! {}", ip);
                return answer;
            }
        }

        std::vector<std::unique_ptr<webrtc::IceCandidateInterface>> new_ice_candidates;
        for(int i = 0; i < candidate_collection->count(); ++i) {
            const webrtc::IceCandidateInterface* ice_candidate = candidate_collection->at(i);
            cricket::Candidate candidate = ice_candidate->candidate();

            rtc::SocketAddress address = candidate.address();
            address.SetIP(ip);
            candidate.set_address(address);

            uint32_t udp_priority = static_cast<uint32_t>(
                    std::min(static_cast<uint64_t>(max_priority) + 1, static_cast<uint64_t>(UINT_MAX)));
            uint32_t tcp_priority = static_cast<uint32_t>(
                    std::min(static_cast<uint64_t>(max_priority) + 2, static_cast<uint64_t>(UINT_MAX)));

            if (candidate.protocol() == "udp") {
                candidate.set_priority(udp_priority);
            }
            else {
                candidate.set_priority(tcp_priority);
            }
            auto new_ice = webrtc::CreateIceCandidate(ice_candidate->sdp_mid(),ice_candidate->sdp_mline_index(),candidate);
            new_ice_candidates.emplace_back(std::move(new_ice));
        }

        for(const auto& new_ice_candidate : new_ice_candidates) {
            session_desc->AddCandidate(new_ice_candidate.get());
            std::string out_string;
            new_ice_candidate->ToString(&out_string);
            LOGI("** AddCandidate {}", out_string);
        }
        std::string sdp;
        if(!session_desc->ToString(&sdp)) {
            LOGE("AddCandidateIpToAnswer failed.");
        }
        return sdp;
    }

}
