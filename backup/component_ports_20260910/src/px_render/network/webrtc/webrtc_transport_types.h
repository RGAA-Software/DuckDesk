#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "px_render/network/transport_types.h"

namespace px {

class Data;
class PxAsyncRuntime;

enum class WebRtcTransportKind {
    kRemote,
    kLocal,
};

enum class WebRtcTransportLifecycle {
    kCreated,
    kRunning,
    kStopping,
    kStopped,
};

enum class WebRtcEncodedVideoType {
    kH264,
    kH265,
    kVp8,
    kVp9,
    kAv1,
};

struct WebRtcTransportConfiguration final {
    std::shared_ptr<PxAsyncRuntime> async_runtime;
    std::string base_path;
    std::wstring base_data_path;
    std::string capture_audio_device_id;
    std::string device_id;
    bool direct_allow_takeover{true};
    bool relay_enabled{true};
    int language{1};
    std::string appkey;
};

struct WebRtcTransportSettings final {
    std::string device_id;
    std::string device_random_password;
    std::string device_safety_password;
    std::string relay_host;
    std::string relay_port;
    bool can_be_operated{true};
    bool direct_allow_takeover{true};
    bool relay_enabled{true};
    int language{1};
    bool file_transfer_enabled{true};
    bool audio_enabled{true};
    std::string appkey;
    std::uint64_t max_transmit_speed{0};
    std::uint64_t max_receive_speed{0};
    int role{1};
};

struct WebRtcTransportInfo final {
    std::string id;
    std::string name;
    std::string author{"RGAA"};
    std::string description;
    std::string version_name;
    std::uint32_t version_code{0};
    bool enabled{true};
};

struct WebRtcNetClientEvent final {
    bool immediate{false};
    bool is_proto{true};
    std::shared_ptr<Data> message;
    TransportKind transport_type{TransportKind::kWebRtc};
    TransportChannel channel_type{};
    std::string connection_instance_id;
};

struct WebRtcClientConnectedEvent final {
    std::string connection_id;
    std::string stream_id;
    std::string connection_type;
    std::string visitor_device_id;
    std::int64_t begin_timestamp{0};
};

struct WebRtcClientDisconnectedEvent final {
    std::string logical_session_id;
    std::string connection_id;
    std::string connection_instance_id;
    std::string stream_id;
    std::string visitor_device_id;
    std::int64_t end_timestamp{0};
    std::int64_t duration{0};
};

struct WebRtcFileTransferDisconnectedEvent final {
    std::string stream_id;
    std::string connection_instance_id;
};

struct WebRtcAnswerSdpEvent final {
    std::string stream_id;
    std::string sdp;
};

struct WebRtcIceEvent final {
    std::string stream_id;
    std::string ice;
    std::string mid;
    int sdp_mline_index{0};
};

struct WebRtcVoicePcmEvent final {
    std::string stream_id;
    std::string call_id;
    std::vector<std::int16_t> pcm;
    int sample_rate{0};
    int channels{0};
};

struct WebRtcInsertIdrEvent final {
    std::string monitor_name;
};

struct WebRtcSelectCaptureMonitorEvent final {
    std::string monitor_name;
};

struct WebRtcConfigureEncoderEvent final {
    std::string monitor_name;
    std::uint32_t bits_per_second{0};
    std::uint32_t frames_per_second{0};
};

using WebRtcEvent = std::variant<WebRtcNetClientEvent, WebRtcClientConnectedEvent, WebRtcClientDisconnectedEvent, WebRtcFileTransferDisconnectedEvent,
                                 WebRtcAnswerSdpEvent, WebRtcIceEvent, WebRtcVoicePcmEvent, WebRtcInsertIdrEvent, WebRtcSelectCaptureMonitorEvent,
                                 WebRtcConfigureEncoderEvent>;

using WebRtcEventCallback = std::function<void(const WebRtcEvent&)>;

} // namespace px
