#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "px_capture_new/capture_message.h"
#include "px_common_new/data.h"
#include "px_common_new/image.h"
#include "px_render/network/transport_types.h"
#include "session/logical_session_registry.h"

namespace px {

enum class EncodedVideoType {
    kH264,
    kH265,
    kVp8,
    kVp9,
    kAv1,
};

struct RedeemConnectionTicketEvent final {
    std::string ticket_;
    std::string client_nonce_;
    std::string instance_id_;
    std::function<void(bool, const std::string&, const std::vector<std::string>&, const std::string&, const std::string&, const std::string&,
                       const std::string&, const std::string&, std::int64_t, bool, bool)>
        callback_;
};

struct AdmitLogicalSessionEvent final {
    LogicalSessionGrant grant_;
    LogicalSessionTransport transport_{LogicalSessionTransport::kWs};
    std::string binding_id_;
    bool takeover_{false};
    std::function<void(LogicalSessionAdmission)> callback_;
};

struct CloseLogicalSessionBindingEvent final {
    std::string logical_session_id_;
    std::string binding_id_;
};

struct NetworkClientEvent final {
    bool is_proto_{true};
    std::shared_ptr<Data> message_;
    std::int64_t socket_fd_{0};
    TransportKind transport_type_{TransportKind::kWebSocket};
    TransportChannel channel_type_{TransportChannel::kMedia};
    std::function<void(const std::shared_ptr<NetMessageAck>&)> ack_callback_;
    std::string connection_instance_id_;
};

struct ClientConnectedEvent final {
    std::string connection_id_;
    std::string stream_id_;
    std::string connection_type_;
    std::string visitor_device_id_;
    std::int64_t begin_timestamp_{0};
};

struct ClientDisconnectedEvent final {
    std::string logical_session_id_;
    std::string connection_id_;
    std::string connection_instance_id_;
    std::string stream_id_;
    std::string visitor_device_id_;
    std::int64_t end_timestamp_{0};
    std::int64_t duration_{0};
};

struct KeyFrameRequestEvent final {
    std::string monitor_name_;
};

struct ReferenceFrameInvalidationEvent final {
    std::string monitor_name_;
    std::uint64_t invalid_frame_index_{0};
};

struct EncodedVideoFrameEvent final {
    EncodedVideoType type_{EncodedVideoType::kH264};
    std::shared_ptr<Data> data_;
    std::uint32_t frame_width_{0};
    std::uint32_t frame_height_{0};
    bool key_frame_{false};
    std::uint64_t frame_index_{0};
    RawImageType frame_format_{RawImageType::kI420};
    CaptureVideoFrame capture_frame_;
};

struct CapturedVideoFrameEvent final {
    CaptureVideoFrame frame_;
};

struct CaptureMonitorInfoChangedEvent final {};

struct CursorUpdatedEvent final {
    CaptureCursorBitmap cursor_info_;
};

struct RelayPausedEvent final {};
struct RelayResumedEvent final {};

struct PanelStreamMessageEvent final {
    std::shared_ptr<Data> body_;
};

struct RelayAliveEvent final {
    std::string device_id_;
};

struct StreamingParametersRequestedEvent final {
    std::string stream_id_;
    bool force_gdi_{false};
};

struct DataSentEvent final {
    std::size_t size_{0};
};

using RenderEvent =
    std::variant<std::shared_ptr<NetworkClientEvent>, std::shared_ptr<ClientConnectedEvent>, std::shared_ptr<ClientDisconnectedEvent>,
                 std::shared_ptr<CaptureMonitorInfoChangedEvent>, std::shared_ptr<KeyFrameRequestEvent>,
                 std::shared_ptr<ReferenceFrameInvalidationEvent>, std::shared_ptr<EncodedVideoFrameEvent>, std::shared_ptr<CapturedVideoFrameEvent>,
                 std::shared_ptr<CursorUpdatedEvent>, std::shared_ptr<RelayPausedEvent>, std::shared_ptr<RelayResumedEvent>,
                 std::shared_ptr<PanelStreamMessageEvent>, std::shared_ptr<RelayAliveEvent>, std::shared_ptr<StreamingParametersRequestedEvent>,
                 std::shared_ptr<RedeemConnectionTicketEvent>, std::shared_ptr<AdmitLogicalSessionEvent>,
                 std::shared_ptr<CloseLogicalSessionBindingEvent>, std::shared_ptr<DataSentEvent>>;

struct RenderEventEnvelope final {
    std::string source_id;
    std::uint64_t created_timestamp{static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())};
    RenderEvent payload;
};

using RenderEventCallback = std::function<void(const RenderEventEnvelope&)>;

} // namespace px
