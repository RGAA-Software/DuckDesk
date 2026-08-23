#include "rtc_plugin.h"

#include "px_common_new/log.h"
#include "px_message.pb.h"
#include "px_render/plugin_interface/px_plugin_events.h"

#include <algorithm>

namespace px
{

    bool IsRtcPayloadAuthorized(const std::string& payload,
                                const std::vector<std::string>& permissions) {
        Message message;
        if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            return false;
        }
        const auto has = [&permissions](const char* permission) {
            return std::find(permissions.begin(), permissions.end(), permission)
                != permissions.end();
        };
        switch (message.type()) {
        case MessageType::kKeyEvent:
        case MessageType::kMouseEvent:
        case MessageType::kGamepadState:
        case MessageType::kReqCtrlAltDelete:
        case MessageType::kTextInput:
            return has("input");
        case MessageType::kClipboardInfo:
        case MessageType::kClipboardInfoResp:
        case MessageType::kClipboardReqAtBegin:
        case MessageType::kClipboardReqBuffer:
        case MessageType::kClipboardReqAtEnd:
        case MessageType::kClipboardRespBuffer:
            return has("clipboard");
        case MessageType::kFileAction:
        case MessageType::kFileResponse:
            return has("file");
        case MessageType::kVoiceCallRequest:
        case MessageType::kVoiceCallResponse:
        case MessageType::kVoiceAudioConfig:
        case MessageType::kVoiceAudioFrame:
            return has("audio");
        default:
            return has("view");
        }
    }

    void RtcPlugin::OnMessage(std::shared_ptr<Message> msg) {
        if (!msg) {
            return;
        }
        const auto type = msg->type();
        if (type == MessageType::kSigOfferSdpMessage) {
            const auto sub = msg->sig_offer_sdp();
            if (sub.connection_ticket().empty() || sub.client_nonce().empty()) {
                LOGW("Reject full RTC offer without Console ticket");
                return;
            }
            auto event = std::make_shared<PxPluginRedeemConnectionTicketEvent>();
            event->ticket_ = sub.connection_ticket();
            event->client_nonce_ = sub.client_nonce();
            event->instance_id_ = sub.instance_id();
            const auto stream_id = msg->stream_id();
            const auto device_id = msg->device_id();
            const auto sdp = sub.sdp();
            event->callback_ = [this, stream_id, device_id, sdp](
                bool ok, const std::string& code,
                const std::vector<std::string>& permissions,
                const std::string& ice_config_json) {
                const bool may_view = std::find(permissions.begin(), permissions.end(), "view")
                    != permissions.end();
                const bool may_file = std::find(permissions.begin(), permissions.end(), "file")
                    != permissions.end();
                if (!ok || (!may_view && !may_file) || ice_config_json.empty()) {
                    LOGW("Reject full RTC ticket: code={}, config_available={}", code,
                         !ice_config_json.empty());
                    return;
                }
                OnMessageRaw(MsgRtcRemoteSdp {
                    .stream_id_ = stream_id,
                    .device_id_ = device_id,
                    .sdp_ = sdp,
                    .ice_config_json_ = ice_config_json,
                    .permissions_ = permissions,
                });
            };
            CallbackEvent(event);
        }
        else if (type == MessageType::kSigIceMessage) {
            const auto sub = msg->sig_ice();
            OnMessageRaw(MsgRtcRemoteIce {
                .stream_id_ = msg->stream_id(),
                .device_id_ = msg->device_id(),
                .ice_ = sub.ice(),
                .mid_ = sub.mid(),
                .sdp_mline_index_ = sub.sdp_mline_index(),
            });
        }
    }

}
