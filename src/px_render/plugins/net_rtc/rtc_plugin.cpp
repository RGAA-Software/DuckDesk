//
// Created RGAA on 15/11/2024.
//

#include "rtc_plugin.h"
#include "px_render/plugins/plugin_ids.h"
#include "video_source_mock.h"
#include "px_common_new/log.h"
#include "rtc_messages.h"
#include "rtc_server.h"
#include "px_common_new/time_util.h"
#include "px_render/plugin_interface/px_plugin_context.h"

#include <algorithm>
#include <cstdint>

namespace px
{

    namespace {
        bool ReadVarint(const std::string& payload, size_t& offset, uint64_t& value) {
            value = 0;
            for (unsigned shift = 0; shift < 64 && offset < payload.size(); shift += 7) {
                const auto byte = static_cast<uint8_t>(payload[offset++]);
                value |= static_cast<uint64_t>(byte & 0x7f) << shift;
                if ((byte & 0x80) == 0) {
                    return true;
                }
            }
            return false;
        }

        bool SkipProtoField(const std::string& payload, size_t& offset, uint32_t wire_type) {
            uint64_t length = 0;
            switch (wire_type) {
            case 0:
                return ReadVarint(payload, offset, length);
            case 1:
                if (payload.size() - offset < 8) return false;
                offset += 8;
                return true;
            case 2:
                if (!ReadVarint(payload, offset, length) || length > payload.size() - offset) return false;
                offset += static_cast<size_t>(length);
                return true;
            case 5:
                if (payload.size() - offset < 4) return false;
                offset += 4;
                return true;
            default:
                return false;
            }
        }

        bool ReadMessageType(const std::string& payload, uint64_t& message_type) {
            size_t offset = 0;
            while (offset < payload.size()) {
                uint64_t tag = 0;
                if (!ReadVarint(payload, offset, tag) || tag == 0) return false;
                const auto field = static_cast<uint32_t>(tag >> 3);
                const auto wire_type = static_cast<uint32_t>(tag & 7);
                if (field == 10) {
                    return wire_type == 0 && ReadVarint(payload, offset, message_type);
                }
                if (!SkipProtoField(payload, offset, wire_type)) return false;
            }
            return false;
        }

        bool HasPermission(const std::vector<std::string>& permissions, const char* permission) {
            return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
        }
    }

    bool IsRtcPayloadAuthorized(const std::string& payload,
                                const std::vector<std::string>& permissions) {
        uint64_t type = 0;
        if (!ReadMessageType(payload, type)) {
            return false;
        }
        switch (type) {
        case 50:  // kKeyEvent
        case 60:  // kMouseEvent
        case 80:  // kGamepadState
        case 330: // kReqCtrlAltDelete
        case 580: // kTextInput
            return HasPermission(permissions, "input");
        case 160: // kClipboardInfo
        case 161: // kClipboardInfoResp
        case 349: // kClipboardReqAtBegin
        case 350: // kClipboardReqBuffer
        case 351: // kClipboardReqAtEnd
        case 360: // kClipboardRespBuffer
            return HasPermission(permissions, "clipboard");
        case 270: // kFileAction
        case 280: // kFileResponse
            return HasPermission(permissions, "file");
        case 590: // kVoiceCallRequest
        case 591: // kVoiceCallResponse
        case 592: // kVoiceAudioConfig
        case 593: // kVoiceAudioFrame
            return HasPermission(permissions, "audio");
        default:
            return HasPermission(permissions, "view");
        }
    }

    std::string RtcPlugin::GetPluginId() {
        return kNetRtcPluginId;
    }

    std::string RtcPlugin::GetPluginName() {
        return "Net RTC";
    }

    std::string RtcPlugin::GetVersionName() {
        return "1.0.2";
    }

    uint32_t RtcPlugin::GetVersionCode() {
        return 102;
    }

    std::string RtcPlugin::GetPluginDescription() {
        return "Network via RTC";
    }

    bool RtcPlugin::OnCreate(const px::PxPluginParam &param) {
        PxNetPlugin::OnCreate(param);

        //
        plugin_context_->StartTimer(100, [=, this]() {
            rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
                srv->On100msTimeout();
            });
        });

        return true;
    }

    bool RtcPlugin::OnDestroy() {
        rtc_servers_.ApplyAll([](const auto&, const std::shared_ptr<RtcServer>& server) {
            server->Exit();
        });
        rtc_servers_.Clear();
        return PxNetPlugin::OnDestroy();
    }

    void RtcPlugin::OnMessageRaw(const std::any& msg) {
        if (HoldsType<MsgRtcRemoteSdp>(msg)) {
            auto m = std::any_cast<MsgRtcRemoteSdp>(msg);
            this->OnRemoteSdp(m);
        }
        else if (HoldsType<MsgRtcRemoteIce>(msg)) {
            auto m = std::any_cast<MsgRtcRemoteIce>(msg);
            this->OnRemoteIce(m);
        }
    }

    void RtcPlugin::PostProtoMessage(std::shared_ptr<Data> msg, bool run_through) {
        WaitForMediaChannelActive();

        rtc_servers_.ApplyAll([=, this](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            srv->PostProtoMessage(msg, run_through);
        });
    }

    bool RtcPlugin::PostTargetStreamProtoMessage(const std::string &stream_id, std::shared_ptr<Data> msg, bool run_through) {
        WaitForMediaChannelActive();

        rtc_servers_.ApplyAll([=, this](const std::string&, const std::shared_ptr<RtcServer>& srv) {
            if (srv && srv->GetStreamId() == stream_id) {
                srv->PostTargetStreamProtoMessage(stream_id, msg, run_through);
            }
        });
        return true;
    }

    bool RtcPlugin::PostTargetFileTransferProtoMessage(
        const std::string& stream_id,
        std::shared_ptr<Data> msg,
        bool run_through,
        const std::string& connection_instance_id) {
        bool accepted = false;
        rtc_servers_.ApplyAll([&](const std::string&, const std::shared_ptr<RtcServer>& srv) {
            const bool matches_connection = connection_instance_id.empty() ||
                (srv && srv->GetConnectionInstanceId() == connection_instance_id);
            if (!accepted && matches_connection && srv && srv->GetStreamId() == stream_id &&
                srv->GetFtPendingMessages() <= 256 &&
                srv->HasEnoughBufferForQueuingFtMessages()) {
                accepted = srv->PostTargetFileTransferProtoMessage(
                    stream_id, msg, run_through);
            }
        });
        return accepted;
    }

    void RtcPlugin::WaitForMediaChannelActive() {
        auto queuing_msg_count = GetQueuingMediaMsgCount();
        auto has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
        auto wait_count = 0;
        while ((queuing_msg_count > 256 || !has_buffer) && wait_count < 2000) {
            if (rtc_servers_.Empty()) {
                LOGW("===> Send media, no alive rtc server, drop the message.");
                return;
            }
            TimeUtil::DelayBySleep(1);
            has_buffer = this->HasEnoughBufferForQueuingMediaMessages();
            queuing_msg_count = GetQueuingMediaMsgCount();
            wait_count++;
        }
        if (wait_count >= 2000) {
            LOGW("===> Send media timeout after {}ms, drop the message, msg count: {}", wait_count, queuing_msg_count);
        }
        else if (wait_count > 0) {
            LOGI("===> Send media wait for: {}ms, msg count: {}", wait_count, queuing_msg_count);
        }
    }

    void RtcPlugin::OnRemoteSdp(const MsgRtcRemoteSdp& m) {
        PostWorkTask([=, this]() {
            auto conn_id = m.device_id_ + ":" + m.stream_id_;
            LOGI("==>OnRemote Offer sdp {} => {}", conn_id, m.sdp_.size());
            auto opt_rtc_server = rtc_servers_.TryGet(conn_id);
            if (opt_rtc_server.has_value()) {
                if (opt_rtc_server.value()->RestartWithOffer(
                        m.sdp_, m.ice_config_json_, m.permissions_)) {
                    LOGI("Reused full RTC peer connection for ICE restart: {}", conn_id);
                    return;
                }
                LOGW("In-place RTC restart failed; replacing peer connection: {}", conn_id);
                opt_rtc_server.value()->Exit();
                rtc_servers_.Remove(conn_id);
            }

            auto rtc_server = RtcServer::Make(this);
            if (rtc_server->Start(
                    m.stream_id_, m.sdp_, m.ice_config_json_, m.permissions_)) {
                rtc_servers_.Insert(conn_id, rtc_server);
            }
            else {
                LOGE("Failed to start full RTC server: {}", conn_id);
            }
        });
    }

    void RtcPlugin::OnRemoteIce(const MsgRtcRemoteIce& m) {
        PostWorkTask([=, this]() {
            auto conn_id = m.device_id_ + ":" + m.stream_id_;
            if (auto opt_rtc_server = rtc_servers_.TryGet(conn_id); opt_rtc_server.has_value()) {
                auto rtc_server = opt_rtc_server.value();
                rtc_server->OnRemoteIce(m.ice_, m.mid_, m.sdp_mline_index_);
            }
        });
    }

    int RtcPlugin::GetConnectedClientsCount() {
        bool has_connected_channel_ = false;
        rtc_servers_.ApplyAll([&](const auto&, const std::shared_ptr<RtcServer>& srv) {
            if (srv->IsDataChannelConnected()) {
                has_connected_channel_ = true;
            }
        });
        return has_connected_channel_;
    }

    int64_t RtcPlugin::GetQueuingMediaMsgCount() {
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            total_pending_messages += srv->GetMediaPendingMessages();
        });
        return total_pending_messages;
    }

    int64_t RtcPlugin::GetQueuingFtMsgCount() {
        // TODO: 连接断开之后，清空srv中的计数
        uint32_t total_pending_messages = 0;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            total_pending_messages += srv->GetFtPendingMessages();
        });
        return total_pending_messages;
    }

    bool RtcPlugin::HasEnoughBufferForQueuingMediaMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            flag &= srv->HasEnoughBufferForQueuingMediaMessages();
        });
        return flag;
    }

    bool RtcPlugin::HasEnoughBufferForQueuingFtMessages() {
        bool flag = true;
        rtc_servers_.ApplyAll([&](const std::string& k, const std::shared_ptr<RtcServer>& srv) {
            flag &= srv->HasEnoughBufferForQueuingFtMessages();
        });
        return flag;
    }

}

PX_PLUGIN_EXPORT(px::RtcPlugin)
