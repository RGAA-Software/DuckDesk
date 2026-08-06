//
// Created RGAA on 15/11/2024.
//

#include "rtc_local_plugin.h"
#include "rtc_server.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "tc_common_new/time_util.h"
#include "gr_render/plugins/plugin_ids.h"
#include "gr_render/plugin_interface/gr_plugin_events.h"
#include "gr_render/plugin_interface/gr_plugin_context.h"

GR_PLUGIN_EXPORT(tc::RtcLocalPlugin)

namespace tc
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
        GrPluginInterface::On1Second();
        SweepDeadRtcServers();
    }

    void RtcLocalPlugin::NotifyRtcServerTerminal(const std::string& conn_id) {
        LOGW("Rtc server terminal notified, conn_id: {}, will be swept.", conn_id);
        rtc_servers_.Apply(conn_id, [](const std::shared_ptr<RtcServer>& srv) {
            srv->RequestExit();
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
            auto removed = rtc_servers_.Remove(k);
            if (removed.has_value() && removed.value() != srv) {
                // 同 key 已被 takeover 的新实例占用,放回去,只清理旧的
                rtc_servers_.Insert(k, removed.value());
            }
            LOGI("Sweep dead rtc server: {}", k);
            srv->Exit();
        }
    }
    
    bool RtcLocalPlugin::OnCreate(const tc::GrPluginParam &param) {
        GrPluginInterface::OnCreate(param);
        plugin_type_ = GrPluginType::kNet;

        if (!IsPluginEnabled()) {
            return true;
        }

        plugin_context_->StartTimer(100, [=, this]() {
            rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
                srv->On100msTimeout();
            });
        });

        return true;
    }

    // 视频/音频帧消息不该走 datachannel:RTC 的音视频走 RTP 轨,web 端也不认识
    // 这类 proto 消息。但 app 会把每个编码帧广播给所有 net 插件
    // (plugin_stream_event_router.cpp VisitNetPlugins→PostProtoMessage),
    // 之前照单全收经 SCTP 转发 => ~9Mbps 的 20KB/帧 消息洪水:
    // render 每帧 PostTask+memcpy、WaitForMediaChannelActive 在帧分发线程自旋,
    // Chrome 主线程每秒 60 次 20KB TLV 重组+proto 解码后丢弃——
    // 主线程被淹正是 web 端"帧率低+完全不跟手"的根因(视频 RTP 轨本身健康)。
    // wire 级窥探 tc.Message 首字段 type(field 1, varint),媒体帧直接丢弃。
    static bool IsMediaFrameMessage(const std::shared_ptr<Data>& msg) {
        if (!msg || msg->Size() < 2) {
            return false;
        }
        const auto* p = (const uint8_t*)msg->DataAddr();
        if (p[0] != 0x08) { // field 1 (type), wire type varint
            return false;
        }
        uint64_t type = 0;
        int shift = 0;
        size_t i = 1;
        while (i < msg->Size() && i < 11) {
            uint8_t b = p[i++];
            type |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) {
                break;
            }
            shift += 7;
        }
        // tc_message.proto: kVideoFrame = 30, kAudioFrame = 40
        return type == 30 || type == 40;
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
            if (srv->IsDataChannelConnected()) {
                count++;
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
                             const GrPluginEncodedVideoType& video_type,
                             const std::shared_ptr<Data>& data,
                             uint64_t frame_index,
                             int frame_width,
                             int frame_height,
                             bool key) {
        // 诊断:确认编码帧是否到达本插件(每 300 帧打一条)
        static std::atomic_uint64_t encoded_frame_count = 0;
        auto ecnt = ++encoded_frame_count;
        if (ecnt == 1 || ecnt % 300 == 0) {
            LOGI("OnEncodedVideoFrame #{}, idx={}, key={}, cache={}", ecnt, frame_index, key, encoded_video_frames_.size());
        }
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
        rtc_servers_.ApplyAll([=, this](const auto&, const std::shared_ptr<RtcServer>& rtc_server) {
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
            if (!srv || srv->IsExitRequested()) {
                return;
            }
            srv->OnRawAudioData(data, samples, channels, bits);
        });
    }

    std::shared_ptr<RtcLocalEncodedVideoFrame> RtcLocalPlugin::PopNextEncodedVideoFrame(const std::string& mon_name, uint64_t after_seq, bool& out_gap) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        out_gap = false;
        // 该屏 seq 严格大于 after_seq 的最旧一帧(序消费,保证 H264 delta 链完整)
        auto it = encoded_video_frames_.upper_bound({mon_name, after_seq});
        if (it == encoded_video_frames_.end() || it->first.first != mon_name) {
            return nullptr;
        }
        // 最旧可取帧的 seq 跳号:中间有未消费的帧被缓存上限淘汰,delta 链已断
        out_gap = (it->first.second != after_seq + 1);
        auto encoded_frame = it->second;
        encoded_video_frames_.erase(it);
        return encoded_frame;
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

    GrLocalRtcAllocResult RtcLocalPlugin::AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& req,
                                                                   std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) {
        auto conn_id = req->device_id_ + ":" + req->stream_id_;
        LOGI("==>AllocNewLocalRtcInstance Offer sdp {} => {}, takeover: {}", conn_id, req->sdp_.size(), req->takeover_);
        auto opt_rtc_server = rtc_servers_.TryGet(conn_id);
        if (opt_rtc_server.has_value()) {
            auto old_server = opt_rtc_server.value();
            // 旧连接的 datachannel 仍活跃且调用方未确认接管:报告占用,由客户端决定
            if (!req->takeover_ && old_server->IsDataChannelConnected()) {
                LOGW("** Occupied by an active connection: {}", conn_id);
                return GrLocalRtcAllocResult::kOccupied;
            }
            LOGI("** Remove old one.");
            // 先从 map 摘除,让新连接立即可建;
            // Exit() 里 webrtc 线程 Stop() 可能因对端会话繁忙而长时间阻塞,
            // 绝不能跑在调用方线程(HTTP 信令线程)上,否则 takeover 请求会挂死信令
            rtc_servers_.Remove(conn_id);
            std::thread([old_server]() {
                old_server->Exit();
            }).detach();
        }

        auto rtc_server = RtcServer::Make(this);
        rtc_server->SetConnId(conn_id);
        rtc_server->Start(req->stream_id_, req->sdp_);
        rtc_server->SetOnAnswerCallback([=, this](const std::string& answer_sdp) {
            auto answer = rtc_server->GetAnswerSdp();
            auto new_answer = AddCandidateIpToAnswer(req->req_ip_, answer);
            auto reply = std::make_shared<GrLocalRtcReplyInfo>(GrLocalRtcReplyInfo {
                .answer_sdp_ = new_answer,
            });
            callback(reply);
        });
        rtc_servers_.Insert(conn_id, rtc_server);
        LOGI("Insert to map, will return information");

        return GrLocalRtcAllocResult::kOk;
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
