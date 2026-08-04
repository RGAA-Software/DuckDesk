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

    void RtcLocalPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        WaitForMediaChannelActive();

        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostProtoMessage(msg, run_through);
        });
    }

    bool RtcLocalPlugin::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
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
        // clear old ones
        if (clear_baseline_timestamp_ > 0) {
            auto it = encoded_video_frames_.begin();
            while (it != encoded_video_frames_.end()) {
                if ((*it).second->timestamp_ <= clear_baseline_timestamp_) {
                    it = encoded_video_frames_.erase(it);
                }
                else {
                    it++;
                }
            }
        }

        auto encoded_video_frame = std::make_shared<RtcLocalEncodedVideoFrame>();
        encoded_video_frame->mon_name_ = mon_name;
        encoded_video_frame->video_type_ = (int)video_type;
        encoded_video_frame->data_ = data;
        encoded_video_frame->frame_index_ = frame_index;
        encoded_video_frame->frame_width_ = frame_width;
        encoded_video_frame->frame_height_ = frame_height;
        encoded_video_frame->key_ = key;
        encoded_video_frame->timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        encoded_video_frames_.insert({mon_name + "#" + std::to_string(frame_index), encoded_video_frame});

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

    std::shared_ptr<RtcLocalEncodedVideoFrame> RtcLocalPlugin::PopEncodedVideoFrame(uint16_t frame_index, const std::string& mon_name) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        const auto exact_key = mon_name + "#" + std::to_string(frame_index);
        if (encoded_video_frames_.contains(exact_key)) {
            auto encoded_frame = encoded_video_frames_[exact_key];
            encoded_video_frames_.erase(exact_key);
            return encoded_frame;
        }
        // 精确序号未命中(编码管线比采集慢时会一直落空):退而取同屏缓存里最新的一帧,
        // 让 webrtc 以编码器的实际产出速率发送,而不是永远等 future 帧。
        // 只匹配同屏帧:切屏过渡期的旧屏残帧绝不能发给已切换的客户端。
        for (auto it = encoded_video_frames_.end(); it != encoded_video_frames_.begin();) {
            --it;
            if (it->second->mon_name_ == mon_name) {
                auto encoded_frame = it->second;
                encoded_video_frames_.erase(it);
                return encoded_frame;
            }
        }
        return nullptr;
    }

    void RtcLocalPlugin::PrintCachedVideoFrames() {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        for (const auto& [frame_idx, frame] : encoded_video_frames_) {
            LOGI("=> frame idx: {} , key: {}", frame_idx, frame->key_);
        }
    }

    void RtcLocalPlugin::SetClearOlderFramesBaseline(int64_t baseline_timestamp) {
        std::lock_guard<std::mutex> lk(encoded_video_frames_mtx_);
        clear_baseline_timestamp_ = baseline_timestamp;
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
