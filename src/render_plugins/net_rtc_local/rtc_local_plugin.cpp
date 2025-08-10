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
        while (queuing_msg_count > 256 ||  !has_buffer) {
            TimeUtil::DelayBySleep(1);
            has_buffer = this->HasEnoughBufferForQueuingFtMessages();
            queuing_msg_count = GetQueuingFtMsgCount();
            wait_count++;
        }
        if (wait_count > 0) {
            //LOGI("===> Send file wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        }
        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostTargetFileTransferProtoMessage(stream_id, msg, run_through);
        });
        return true;
    }

    void RtcLocalPlugin::WaitForMediaChannelActive() {
        auto queuing_msg_count = GetQueuingMediaMsgCount();
        auto has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        auto wait_count = 0;
        while (queuing_msg_count > 256 ||  !has_buffer) {
            TimeUtil::DelayBySleep(1);
            has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
            queuing_msg_count = GetQueuingMediaMsgCount();
            wait_count++;
        }
        if (wait_count > 0) {
            LOGI("===> Send media wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        }
    }

    int RtcLocalPlugin::GetConnectedClientsCount() {
        bool has_connected_channel_ = false;
        rtc_servers_.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
            if (srv->IsDataChannelConnected()) {
                has_connected_channel_ = true;
            }
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

    bool RtcLocalPlugin::AllocNewLocalRtcInstance(const std::shared_ptr<GrLocalRtcRequestInfo>& req,
                                                  std::function<void(const std::shared_ptr<GrLocalRtcReplyInfo>&)>&& callback) {
        auto conn_id = req->device_id_ + ":" + req->stream_id_;
        LOGI("==>AllocNewLocalRtcInstance Offer sdp {} => {}", conn_id, req->sdp_.size());
        auto opt_rtc_server = rtc_servers_.TryGet(conn_id);
        if (opt_rtc_server.has_value()) {
            LOGI("Remove old one.");
            opt_rtc_server.value()->Exit();
            rtc_servers_.Remove(conn_id);
        }

        auto rtc_server = RtcServer::Make(this);
        rtc_server->Start(req->stream_id_, req->sdp_);
        rtc_server->SetOnAnswerCallback([=, this](const std::string& answer_sdp) {
            auto answer = rtc_server->GetAnswerSdp();
            auto new_answer = AddCandidateIpToAnwser(req->req_ip_, answer);
//            LOGI("answer: {}", answer);
//            LOGI("new answer, add ip: {}, {}", req->req_ip_, new_answer);
            auto reply = std::make_shared<GrLocalRtcReplyInfo>(GrLocalRtcReplyInfo {
                .answer_sdp_ = new_answer,
            });
            callback(reply);
        });
        rtc_servers_.Insert(conn_id, rtc_server);
        LOGI("Insert to map, will return information");

        return true;
    }

    std::string RtcLocalPlugin::AddCandidateIpToAnwser(const std::string& ip,const std::string& anwser) {

        std::unique_ptr<webrtc::SessionDescriptionInterface> session_desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer,anwser);

        const webrtc::IceCandidateCollection* candidate_collection = session_desc->candidates(0);

        // 寻找一个优先级最低的。让添加的外网ip的优先级最低。
        // 同时检测是否已经存在相同IP,相同则直接返回不添加。
        uint32_t max_priority = 0;
        for (int i = 0; i < candidate_collection->count(); ++i) {
            const webrtc::IceCandidateInterface* ice_candidate = candidate_collection->at(i);
            const cricket::Candidate& candidate = ice_candidate->candidate();
            if(candidate.priority() > max_priority)
                max_priority = candidate.priority();

            // 已经存在该IP,则直接返回。
            if(candidate.address().EqualIPs(rtc::SocketAddress(ip,0))) {
                LOGI("Found same! {}", ip);
                return anwser;
            }
        }

        std::vector<std::unique_ptr<webrtc::IceCandidateInterface>> new_ice_candidates;

        LOGI("Candidate counts: {}", candidate_collection->count());
        for(int i = 0; i < candidate_collection->count(); ++i) {
            const webrtc::IceCandidateInterface* ice_candidate = candidate_collection->at(i);
            cricket::Candidate candidate = ice_candidate->candidate();

            // 替换IP
            rtc::SocketAddress address = candidate.address();
            address.SetIP(ip);
            candidate.set_address(address);

            // 让UDP的优先级高一点。
            uint32_t udp_priority = static_cast<uint32_t>(
                    std::min(static_cast<uint64_t>(max_priority) + 1, static_cast<uint64_t>(UINT_MAX)));

            uint32_t tcp_priority = static_cast<uint32_t>(
                    std::min(static_cast<uint64_t>(max_priority) + 2, static_cast<uint64_t>(UINT_MAX)));

            if (candidate.protocol() == "udp")
                candidate.set_priority(udp_priority);
            else
                candidate.set_priority(tcp_priority);

            auto new_ice = webrtc::CreateIceCandidate(ice_candidate->sdp_mid(),ice_candidate->sdp_mline_index(),candidate);
            new_ice_candidates.emplace_back(std::move(new_ice));
        }

        for(int i = 0; i < new_ice_candidates.size(); ++i) {
            session_desc->AddCandidate(new_ice_candidates[i].get());

            std::string out_string;
            new_ice_candidates[i]->ToString(&out_string);
            LOGI("AddCandidate {}", out_string);
        }
        std::string ret_sdp;
        if(!session_desc->ToString(&ret_sdp)) {
            LOGE("AddCandidateIpToAnwser to sdp failed.");
        }
        return ret_sdp;
    }

}
