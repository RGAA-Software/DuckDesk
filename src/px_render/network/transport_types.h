#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace px {

enum class TransportKind {
    kWebSocket,
    kUdpKcp,
    kWebRtcDirect,
    kWebRtc,
};

enum class TransportChannel {
    kMedia,
    kFileTransfer,
};

class NetMessageAck {
public:
    std::uint64_t send_time_{0};
    std::uint64_t resp_time_{0};
    TransportChannel ch_type_{TransportChannel::kMedia};
    int msg_type_{0};
};

class NetSyncInfo {
public:
    std::int64_t socket_fd_{0};
    std::string device_id_;
    std::string stream_id_;
};

class PxConnectedClientInfo {
public:
    std::string device_id_;
    std::string stream_id_;
    std::string relay_room_id_;
    std::string device_name_;
};

enum class PxLocalRtcContentType {
    kDesktop,
    kGameStream,
};

enum class PxLocalRtcSessionRole {
    kInteractive,
    kObserver,
    kWallObserver,
};

class PxLocalRtcRequestInfo {
public:
    std::string device_id_;
    std::string stream_id_;
    std::string req_ip_;
    std::string sdp_;
    PxLocalRtcContentType content_type_{PxLocalRtcContentType::kDesktop};
    PxLocalRtcSessionRole session_role_{PxLocalRtcSessionRole::kInteractive};
    bool takeover_{false};
    std::string client_nonce_;
    bool capability_enforced_{false};
    std::vector<std::string> permissions_;
};

enum class PxLocalRtcAllocResult {
    kOk,
    kOccupied,
    kFailed,
};

class PxLocalRtcMonitorInfo {
public:
    std::string name_;
    int width_{0};
    int height_{0};
    int left_{0};
    int top_{0};
    int right_{0};
    int bottom_{0};
};

class PxLocalRtcReplyInfo {
public:
    std::string answer_sdp_;
    std::vector<PxLocalRtcMonitorInfo> monitors_;
};

class PxLogicalSessionCapabilityUpdate {
public:
    std::string stream_id_;
    std::vector<std::string> permissions_;
};

struct UdpMediaAssociation final {
    std::string association_code_;
    std::string logical_session_id_;
    std::string stream_id_;
    std::int64_t expires_at_ms_{0};
    bool force_gdi_{false};
    bool revoke_{false};
};

}  // namespace px
