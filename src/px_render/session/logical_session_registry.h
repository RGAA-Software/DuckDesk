#ifndef PX_LOGICAL_SESSION_REGISTRY_H
#define PX_LOGICAL_SESSION_REGISTRY_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace px {

enum class LogicalSessionRole {
    kController,
    kObserver,
};

enum class LogicalSessionTransport {
    kWs,
    kRtcLocal,
    kRtc,
    kUdp,
    kRelay,
    kFileTransfer,
};

enum class LogicalSessionAdmissionCode {
    kAccepted,
    kOccupied,
    kObserversDisabled,
    kTakeoverDisabled,
    kInvalidGrant,
    kExpired,
};

struct LogicalSessionGrant {
    std::string logical_session_id;
    std::string stream_id;
    std::string subject_id;
    std::string join_mode;
    int64_t expires_at_ms = 0;
    bool allow_observer = true;
    bool allow_takeover = true;
    // Explicitly populated by ticket adapters. Trusted pre-ticket/native callers retain their existing control grant semantics.
    bool input_allowed{true};
};

struct LogicalSessionAdmission {
    LogicalSessionAdmissionCode code = LogicalSessionAdmissionCode::kInvalidGrant;
    LogicalSessionRole role = LogicalSessionRole::kObserver;
    uint64_t lease_generation = 0;
    bool release_previous_controller_input = false;
    std::string previous_controller_session_id;
    uint64_t previous_controller_lease_generation = 0;
};

struct LogicalSessionBindingClosed {
    bool logical_session_closed = false;
    bool release_controller_input = false;
    std::string logical_session_id;
    uint64_t lease_generation = 0;
};

// Immutable controller identity used by input routing. The generation makes a
// queued event from a replaced controller unambiguously stale.
struct LogicalSessionInputLease {
    std::string logical_session_id;
    std::string binding_id;
    uint64_t generation = 0;
    uint64_t binding_generation = 0;
    uint64_t input_capability_generation{0};
};

struct LogicalSessionSnapshot {
    std::string logical_session_id;
    std::string takeover_previous_session_id;
    std::string stream_id;
    std::string subject_id;
    LogicalSessionRole role = LogicalSessionRole::kObserver;
    std::vector<LogicalSessionTransport> transports;
};

/// Thread-safe ownership boundary for the desktop remote-control product.
/// Physical network bindings are transient; role and controller lease state
/// are owned here and are never inferred from a plugin connection count.
class LogicalSessionRegistry final {
public:
    explicit LogicalSessionRegistry(bool allow_observer = true, bool allow_takeover = true,
                                    int64_t controller_reconnect_grace_ms = 5000);

    LogicalSessionRegistry(const LogicalSessionRegistry&) = delete;
    LogicalSessionRegistry& operator=(const LogicalSessionRegistry&) = delete;

    void SetPolicy(bool allow_observer, bool allow_takeover);
    void UpdateInputCapabilityByStream(const std::string& stream_id, bool allowed);

    LogicalSessionAdmission Bind(const LogicalSessionGrant& grant,
                                 LogicalSessionTransport transport,
                                 const std::string& binding_id,
                                 bool takeover,
                                 int64_t now_ms);

    LogicalSessionBindingClosed CloseBinding(const std::string& logical_session_id,
                                             const std::string& binding_id,
                                             int64_t now_ms);
    LogicalSessionBindingClosed CloseBindingById(const std::string& binding_id,
                                                 int64_t now_ms);

    bool AuthorizeControllerInput(const std::string& logical_session_id,
                                  uint64_t lease_generation,
                                  int64_t now_ms) const;
    bool AuthorizeControllerInputBinding(const std::string& binding_id,
                                         int64_t now_ms) const;
    bool AuthorizeControllerInputStream(const std::string& stream_id,
                                        int64_t now_ms) const;
    std::optional<LogicalSessionInputLease> FindControllerInputLeaseByBinding(
        const std::string& binding_id, int64_t now_ms) const;
    // Auxiliary channels do not create a binding or extend occupancy. Their
    // authenticated grant must match an existing physical controller binding.
    std::optional<LogicalSessionInputLease> AuthorizeAuxiliaryInputGrant(const LogicalSessionGrant& grant,
                                                                       const std::string& parent_binding_id, int64_t now_ms) const;
    bool IsCurrentInputBinding(const LogicalSessionInputLease& lease, int64_t now_ms) const;
    // File-transfer bindings may establish a Controller logical session, but
    // they can never authorize OS input. This lookup is only for Controller-
    // scoped auxiliary capabilities such as file transfer.
    std::optional<LogicalSessionInputLease> FindControllerLeaseByBinding(
        const std::string& binding_id, int64_t now_ms) const;
    std::optional<LogicalSessionInputLease> FindControllerInputLeaseByStream(
        const std::string& stream_id, int64_t now_ms) const;
    std::optional<std::string> FindLogicalSessionIdByBinding(
        const std::string& binding_id, int64_t now_ms) const;

    std::optional<LogicalSessionRole> FindRole(const std::string& logical_session_id) const;
    std::optional<std::string> FindStreamId(const std::string& logical_session_id) const;
    std::vector<LogicalSessionSnapshot> SnapshotActive(int64_t now_ms) const;
    size_t ActiveSessionCount() const;
    size_t ObserverCount() const;

private:
    struct Binding {
        LogicalSessionTransport transport = LogicalSessionTransport::kWs;
        std::string binding_id;
        uint64_t generation{0};
        bool input_allowed{true};
    };

    struct Session {
        std::string stream_id;
        std::string subject_id;
        LogicalSessionRole role = LogicalSessionRole::kObserver;
        int64_t expires_at_ms = 0;
        uint64_t lease_generation = 0;
        std::string takeover_previous_session_id;
        int64_t controller_disconnected_at_ms = 0;
        bool allow_observer = true;
        bool allow_takeover = true;
        bool input_allowed{true};
        std::unordered_map<std::string, Binding> bindings;
        uint64_t input_capability_generation{0};
    };

    bool HasControllerBinding(const Session& session) const;
    bool HasInputBinding(const Session& session) const;
    LogicalSessionBindingClosed CloseBindingLocked(
        std::unordered_map<std::string, Session>::iterator session_it,
        const std::string& binding_id, int64_t now_ms);
    void RemoveStaleSessionsLocked(int64_t now_ms);
    LogicalSessionAdmission AdoptControllerLocked(const LogicalSessionGrant& grant,
                                                  LogicalSessionTransport transport,
                                                  const std::string& binding_id,
                                                  bool takeover,
                                                  int64_t now_ms);

    mutable std::mutex mutex_;
    bool allow_observer_ = true;
    bool allow_takeover_ = true;
    int64_t controller_reconnect_grace_ms_ = 5000;
    std::unordered_map<std::string, Session> sessions_;
    std::string controller_session_id_;
    uint64_t next_binding_generation_{1};
    uint64_t next_input_capability_generation_{1};
};

} // namespace px

#endif // PX_LOGICAL_SESSION_REGISTRY_H
