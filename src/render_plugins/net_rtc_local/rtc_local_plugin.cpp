//
// Created RGAA on 15/11/2024.
//

#include "rtc_local_plugin.h"
#include "rtc_server.h"
#include "tc_common_new/log.h"
#include "tc_common_new/file.h"
#include "tc_common_new/image.h"
#include "render/plugins/plugin_ids.h"
#include "plugin_interface/gr_plugin_events.h"
#include "plugin_interface/gr_plugin_context.h"

void* GetInstance() {
    static tc::RtcLocalPlugin plugin;
    return (void*)&plugin;
}

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

        //rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
        //    srv->PostProtoMessage(msg, run_through);
        //});
    }

    bool RtcLocalPlugin::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        WaitForMediaChannelActive();

        //rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
        //    srv->PostTargetStreamProtoMessage(stream_id, msg, run_through);
        //});
        return true;
    }

    bool RtcLocalPlugin::PostTargetFileTransferProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        //auto queuing_msg_count = GetQueuingFtMsgCount();
        //auto has_buffer = this->HasEnoughBufferForQueuingFtMessages();
        //auto wait_count = 0;
        //while (queuing_msg_count > 256 ||  !has_buffer) {
        //    TimeUtil::DelayBySleep(1);
        //    has_buffer = this->HasEnoughBufferForQueuingFtMessages();
        //    queuing_msg_count = GetQueuingFtMsgCount();
        //    wait_count++;
        //}
        //if (wait_count > 0) {
        //    //LOGI("===> Send file wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        //}
        //rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
        //    srv->PostTargetFileTransferProtoMessage(stream_id, msg, run_through);
        //});
        return true;
    }

    void RtcLocalPlugin::WaitForMediaChannelActive() {
        //auto queuing_msg_count = GetQueuingMediaMsgCount();
        //auto has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        //auto wait_count = 0;
        //while (queuing_msg_count > 256 ||  !has_buffer) {
        //    TimeUtil::DelayBySleep(1);
        //    has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        //    queuing_msg_count = GetQueuingMediaMsgCount();
        //    wait_count++;
        //}
        //if (wait_count > 0) {
        //    LOGI("===> Send media wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        //}
    }

    int RtcLocalPlugin::GetConnectedClientsCount() {
        bool has_connected_channel_ = false;
        rtc_servers_.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
            //if (srv->IsDataChannelConnected()) {
                has_connected_channel_ = true;
            //}
        });
        return has_connected_channel_;
    }

    int64_t RtcLocalPlugin::GetQueuingMediaMsgCount() {
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            total_pending_messages += srv->GetMediaPendingMessages();
        });
        return total_pending_messages;
    }

    int64_t RtcLocalPlugin::GetQueuingFtMsgCount() {
        // TODO: 连接断开之后，清空srv中的计数
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            total_pending_messages += srv->GetFtPendingMessages();
        });
        return total_pending_messages;
    }

    bool RtcLocalPlugin::HasEnoughBufferForQueuingMediaMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            flag &= srv->HasEnoughBufferForQueuingMediaMessages();
        });
        return flag;
    }

    bool RtcLocalPlugin::HasEnoughBufferForQueuingFtMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
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

        //auto encoded_video_frame = std::make_shared<RtcLocalEncodedVideoFrame>();
        //encoded_video_frame->mon_name_ = mon_name;
        //encoded_video_frame->video_type_ = (int)video_type;
        //encoded_video_frame->data_ = data;
        //encoded_video_frame->frame_index_ = frame_index;
        //encoded_video_frame->frame_width_ = frame_width;
        //encoded_video_frame->frame_height_ = frame_height;
        //encoded_video_frame->key_ = key;
        //encoded_video_frame->timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        //encoded_video_frames_.insert({frame_index, encoded_video_frame});

    }

    // raw video frame
    // handle: D3D Shared texture handle
    void RtcLocalPlugin::OnRawVideoFrameSharedTexture(const std::string& mon_name, uint64_t frame_idx, int frame_width, int frame_height, uint64_t handle, int64_t adapter_id, uint64_t frame_format) {
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

    std::shared_ptr<RtcLocalEncodedVideoFrame> RtcLocalPlugin::PopEncodedVideoFrame(uint16_t frame_index) {
        if (encoded_video_frames_.contains(frame_index)) {
            auto encoded_frame = encoded_video_frames_[frame_index];
            encoded_video_frames_.erase(frame_index);
            return encoded_frame;
        }
        return nullptr;
    }

    void RtcLocalPlugin::PrintCachedVideoFrames() {
        for (const auto& [frame_idx, frame] : encoded_video_frames_) {
            LOGI("=> frame idx: {} , key: {}", frame_idx, frame->key_);
        }
    }

    void RtcLocalPlugin::SetClearOlderFramesBaseline(int64_t baseline_timestamp) {
        clear_baseline_timestamp_ = baseline_timestamp;
    }

    bool RtcLocalPlugin::AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& req,
                                                  std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) {
        auto conn_id = req->device_id_ + ":" + req->stream_id_;
        LOGI("==>AllocNewLocalRtcInstance Offer sdp {} => {}", conn_id, req->sdp_.size());
        auto opt_rtc_server = rtc_servers_.TryGet(conn_id);
        if (opt_rtc_server.has_value()) {
            LOGI("** Remove old one.");
            opt_rtc_server.value()->Exit();
            rtc_servers_.Remove(conn_id);
        }

        auto rtc_server = RtcServer::Make(this);
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

        return true;
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
