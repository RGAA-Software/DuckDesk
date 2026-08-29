//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_PLUGIN_EVENTS_H
#define PX_RENDER_PLUGIN_EVENTS_H

#include <string>
#include <atomic>
#include <memory>
#include <functional>
#include <vector>
#include "px_net_plugin.h"
#include "px_plugin_interface.h"
#include "px_common_new/image.h"
#include "px_common_new/time_util.h"
#include "px_capture_new/capture_message.h"

namespace px
{

    class Data;
    class Image;

    enum class PxPluginEventType {
        kPluginUnknownType,
        kPluginNetClientEvent,
        kPluginClientConnectedEvent,
        kPluginClientDisConnectedEvent,
        kPluginInsertIdrEvent,
        kPluginInvalidateRefFrameEvent,
        kPluginEncodedVideoFrameEvent,
        kPluginCapturingMonitorInfoEvent,
        kPluginCapturedVideoFrameEvent,
        kPluginCursorEvent,
        kPluginRawVideoFrameEvent,
        kPluginRawAudioFrameEvent,
        kPluginSplitRawAudioFrameEvent,
        kPluginEncodedAudioFrameEvent,
        kPluginRelayPausedEvent,
        kPluginRelayResumeEvent,
        kPluginRelayAlive,
        kPluginRtcAnswerSdpEvent,
        kPluginRtcIceEvent,
        kPluginRtcReportEvent,
        kPluginFileTransferBegin,
        kPluginFileTransferEnd,
        kPluginDataSent,
        kPluginRemoteClipboardResp,
        kPluginPanelStreamMessage,
        kPluginConfigEncoder,
        kPluginReqParamsBeginStreaming,
        kPluginRedeemConnectionTicket,
        kPluginVoiceCallConsent,
        kPluginVoiceCallMedia,
    };

    class PxPluginBaseEvent {
    public:
        PxPluginBaseEvent() {
            created_timestamp_ = TimeUtil::GetCurrentTimestamp();
        }
        virtual ~PxPluginBaseEvent() = default;
    public:
        std::string plugin_name_;
        PxPluginEventType event_type_{PxPluginEventType::kPluginUnknownType};
        std::any extra_;
        uint64_t created_timestamp_ = 0;
    };

    class PxPluginRedeemConnectionTicketEvent : public PxPluginBaseEvent {
    public:
        PxPluginRedeemConnectionTicketEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRedeemConnectionTicket;
        }
        std::string ticket_;
        std::string client_nonce_;
        std::string instance_id_;
        std::function<void(bool, const std::string&, const std::vector<std::string>&,
                           const std::string&)> callback_;
    };

    // Voice plugin -> Render -> px_panel. show_=false closes only an exactly
    // matching pending prompt and never constitutes a user decision.
    class PxPluginVoiceCallConsentEvent : public PxPluginBaseEvent {
    public:
        PxPluginVoiceCallConsentEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginVoiceCallConsent;
        }
        bool show_ = true;
        std::string visitor_device_id_;
        std::string stream_id_;
        std::string call_id_;
        uint64_t request_id_ = 0;
        uint64_t expires_at_unix_ms_ = 0;
        std::string reason_;
    };

    enum class PxVoiceCallMediaAction {
        kStreamMessage,
        kRtcAuthorization,
        kRtcPcm,
    };

    // Owned envelope used by the voice runtime. Audio and serialized messages
    // remain valid until routing completes; no plug-in instance or borrowed
    // network pointer crosses the asynchronous boundary.
    class PxPluginVoiceCallMediaEvent : public PxPluginBaseEvent {
    public:
        PxPluginVoiceCallMediaEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginVoiceCallMedia;
        }
        PxVoiceCallMediaAction action_ = PxVoiceCallMediaAction::kStreamMessage;
        std::string stream_id_;
        std::string call_id_;
        std::shared_ptr<Data> message_;
        bool authorized_ = false;
        std::shared_ptr<std::atomic_bool> authorization_applied_;
        std::vector<int16_t> pcm_;
        int sample_rate_ = 0;
        int channels_ = 0;
    };

    // kPluginNetClientEvent
    class PxPluginNetClientEvent : public PxPluginBaseEvent {
    public:
        PxPluginNetClientEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginNetClientEvent;
        }
    public:
        bool is_proto_ = true;
        std::shared_ptr<Data> message_;
        int64_t socket_fd_ = 0;
        NetPluginType nt_plugin_type_;
        NetChannelType nt_channel_type_;
        PxNetPlugin* from_plugin_ = nullptr;
        // New transports use value identity and an owned acknowledgement sink
        // so their asynchronous receive path never has to retain a plug-in
        // instance. from_plugin_ remains only for the established plug-in ABI.
        std::string source_plugin_id_;
        std::function<void(const std::shared_ptr<NetMessageAck>&)> ack_callback_;
        // Value identity of the concrete transport connection. It is copied
        // into FT routing state and never used as an ownership handle.
        std::string connection_instance_id_;
    };

    // Value envelope: copy the source id at the established plug-in ABI
    // boundary so file-transfer code never retains or guesses a transport.
    struct FtInboundMessage {
        std::shared_ptr<Message> message_;
        std::string source_plugin_id_;
        std::string source_connection_id_;
    };

    // A transport-specific disconnect must only remove the route currently
    // owned by that transport. This avoids a late WS close tearing down an RTC
    // file-transfer route for the same stream.
    struct FtRouteDisconnected {
        std::string stream_id_;
        std::string source_plugin_id_;
        std::string source_connection_id_;
    };

    // PxClientConnectedEvent
    class PxPluginClientConnectedEvent : public PxPluginBaseEvent {
    public:
        PxPluginClientConnectedEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginClientConnectedEvent;
        }
    public:
        std::string conn_id_;
        std::string stream_id_;
        std::string conn_type_;
        std::string visitor_device_id_;
        int64_t begin_timestamp_ = 0;
    };

    // PxClientDisConnectedEvent
    class PxPluginClientDisConnectedEvent : public PxPluginBaseEvent {
    public:
        PxPluginClientDisConnectedEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginClientDisConnectedEvent;
        }
    public:
        std::string conn_id_;
        std::string stream_id_;
        std::string visitor_device_id_;
        int64_t end_timestamp_ = 0;
        int64_t duration_ = 0;
    };

    // PxPluginInsertIdrEvent
    class PxPluginInsertIdrEvent : public PxPluginBaseEvent {
    public:
        PxPluginInsertIdrEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginInsertIdrEvent;
        }
    public:
        // 目标显示器名:空 = 广播给所有屏(旧行为,WS/Relay 等调用方不变);
        // 非空 = 只给该屏的编码器补 IDR(RTC 多 track 时按屏定向,
        // 避免一条 track 的 PLI 让所有屏同时刷 IDR)
        std::string mon_name_;
    };

    // kPluginInvalidateRefFrameEvent:客户端丢失整帧后优先请求 RFI,render 让
    // NVENC 调用 NvEncInvalidateRefFrames() 跳过坏参考帧,而不是立刻插 IDR。
    class PxPluginInvalidateRefFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginInvalidateRefFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginInvalidateRefFrameEvent;
        }
    public:
        std::string mon_name_;
        uint64_t invalid_frame_index_ = 0;
    };

    // PxPluginEncodedVideoFrameEvent
    class PxPluginEncodedVideoFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginEncodedVideoFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginEncodedVideoFrameEvent;
        }
    public:
        PxPluginEncodedVideoType type_;
        std::shared_ptr<Data> data_ = nullptr;
        uint32_t frame_width_ = 0;
        uint32_t frame_height_ = 0;
        bool key_frame_ = false;
        uint64_t frame_index_ = 0;
        RawImageType frame_format_ = RawImageType::kI420;
    };

    //
    class PxPluginCapturedVideoFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginCapturedVideoFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginCapturedVideoFrameEvent;
        }
    public:
        CaptureVideoFrame frame_;
    };

    //
    class PxPluginCapturingMonitorInfoEvent : public PxPluginBaseEvent {
    public:
        PxPluginCapturingMonitorInfoEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginCapturingMonitorInfoEvent;
        }
    public:

    };

    //
    class PxPluginCursorEvent : public PxPluginBaseEvent {
    public:
        PxPluginCursorEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginCursorEvent;
        }
    public:
        CaptureCursorBitmap cursor_info_;
    };

    // Raw video frame from plugins
    class PxPluginRawVideoFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginRawVideoFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRawVideoFrameEvent;
        }
    public:
        std::shared_ptr<Image> image_ = nullptr;
        uint64_t frame_index_ = 0;
        uint64_t frame_format_ = 0;
    };

    // Raw audio frame from plugins
    class PxPluginRawAudioFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginRawAudioFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRawAudioFrameEvent;
        }
    public:
        // left/right/left/right...
        std::shared_ptr<Data> full_data_ = nullptr;
        //
        int sample_rate_ = 0;
        int channels_ = 0;
        int bits_ = 0;
    };

    // Raw audio frame
    class PxPluginSplitRawAudioFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginSplitRawAudioFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginSplitRawAudioFrameEvent;
        }
    public:
        // left/left/left/...
        std::shared_ptr<Data> left_ch_data_ = nullptr;
        // right/right/right...
        std::shared_ptr<Data> right_ch_data_ = nullptr;
        //
        int sample_rate_ = 0;
        int channels_ = 0;
        int bits_ = 0;
    };

    // Encode audio frame
    class PxPluginEncodedAudioFrameEvent : public PxPluginBaseEvent {
    public:
        PxPluginEncodedAudioFrameEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginEncodedAudioFrameEvent;
        }
    public:
        int sample_rate_ = 0;
        int channels_ = 0;
        int bits_ = 0;
        int frame_size_ = 0;
        std::shared_ptr<Data> data_ = nullptr;
    };

    // relay paused
    class PxPluginRelayPausedEvent : public PxPluginBaseEvent {
    public:
        PxPluginRelayPausedEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRelayPausedEvent;
        }
    };

    // relay resumed
    class PxPluginRelayResumedEvent : public PxPluginBaseEvent {
    public:
        PxPluginRelayResumedEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRelayResumeEvent;
        }
    };

    // rtc answer
    class PxPluginRtcAnswerSdpEvent : public PxPluginBaseEvent {
    public:
        PxPluginRtcAnswerSdpEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRtcAnswerSdpEvent;
        }

    public:
        std::string stream_id_;
        std::string sdp_;
    };

    // rtc ice
    class PxPluginRtcIceEvent : public PxPluginBaseEvent {
    public:
        PxPluginRtcIceEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRtcIceEvent;
        }
    public:
        std::string stream_id_;
        std::string ice_;
        std::string mid_;
        int sdp_mline_index_{0};
    };

    // rtc report event
    class PxPluginRtcReportEvent : public PxPluginBaseEvent {
    public:
        PxPluginRtcReportEvent() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRtcReportEvent;
        }
    public:
        std::string evt_name_;
        std::string msg_;
        std::string data_channel_name_;
    };

    // file transfer begin
    class PxPluginFileTransferBegin : public PxPluginBaseEvent {
    public:
        PxPluginFileTransferBegin() {
            event_type_ = PxPluginEventType::kPluginFileTransferBegin;
        }
    public:
        std::string the_file_id_;
        int64_t begin_timestamp_ = 0;
        std::string visitor_device_id_;
        std::string direction_;
        std::string file_detail_;
    };

    // file transfer end
    class PxPluginFileTransferEnd : public PxPluginBaseEvent {
    public:
        PxPluginFileTransferEnd() {
            event_type_ = PxPluginEventType::kPluginFileTransferEnd;
        }
    public:
        bool success_ = false;
        std::string the_file_id_;
        int64_t end_timestamp_ = 0;
        int64_t duration_ = 0;
        std::string status_;
        std::string end_reason_;
    };

    // data sent size
    class PxPluginDataSent : public PxPluginBaseEvent {
    public:
        PxPluginDataSent() {
            event_type_ = PxPluginEventType::kPluginDataSent;
        }
    public:
        int size_ = 0;
    };

    // remote clipboard resp
    class PxPluginRemoteClipboardResp : public PxPluginBaseEvent {
    public:
        PxPluginRemoteClipboardResp() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRemoteClipboardResp;
        }
    public:
        // text / file
        int content_type_ {0};
        // text content
        std::string remote_info_;
    };

    // panel stream message
    // request from remote panel
    class PxPluginPanelStreamMessage : public PxPluginBaseEvent {
    public:
        PxPluginPanelStreamMessage() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginPanelStreamMessage;
        }
    public:
        std::shared_ptr<Data> body_ = nullptr;
    };

    // config encoder
    class PxPluginConfigEncoder : public PxPluginBaseEvent {
    public:
        PxPluginConfigEncoder() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginConfigEncoder;
        }
    public:
        std::string mon_name_;
        uint32_t bps_ = 0;
        uint32_t fps_ = 0;
    };

    // relay plugin alive
    class PxPluginRelayAlive : public PxPluginBaseEvent {
    public:
        PxPluginRelayAlive() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginRelayAlive;
        }
    public:
        std::string device_id_;
    };

    // kPluginReqParamsBeginStreaming
    class PxPluginReqParamsBeginStreaming : public PxPluginBaseEvent {
    public:
        PxPluginReqParamsBeginStreaming() : PxPluginBaseEvent() {
            event_type_ = PxPluginEventType::kPluginReqParamsBeginStreaming;
        }
    public:
        std::string stream_id_;
        bool force_gdi_ = false;
    };

}

#endif //PX_RENDER_PLUGIN_EVENTS_H
