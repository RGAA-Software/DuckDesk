#include "logical_session_registry.h"

#include <algorithm>

namespace px {

LogicalSessionRegistry::LogicalSessionRegistry(const bool allow_observer,
                                               const bool allow_takeover,
                                               const int64_t controller_reconnect_grace_ms)
    : allow_observer_(allow_observer),
      allow_takeover_(allow_takeover),
      controller_reconnect_grace_ms_(std::max<int64_t>(0, controller_reconnect_grace_ms)) {}

void LogicalSessionRegistry::SetPolicy(const bool allow_observer, const bool allow_takeover) {
    std::scoped_lock lock(mutex_);
    allow_observer_ = allow_observer;
    allow_takeover_ = allow_takeover;
}

bool LogicalSessionRegistry::HasControllerBinding(const Session& session) const {
    return std::any_of(session.bindings.begin(), session.bindings.end(),
        [](const auto& item) {
            return item.second.transport != LogicalSessionTransport::kFileTransfer;
        });
}

void LogicalSessionRegistry::RemoveStaleSessionsLocked(const int64_t now_ms) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const bool stale_controller = it->second.role == LogicalSessionRole::kController
            && it->second.bindings.empty()
            && it->second.controller_disconnected_at_ms > 0
            && now_ms - it->second.controller_disconnected_at_ms > controller_reconnect_grace_ms_;
        if (stale_controller) {
            if (controller_session_id_ == it->first) {
                controller_session_id_.clear();
            }
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

LogicalSessionAdmission LogicalSessionRegistry::AdoptControllerLocked(
    const LogicalSessionGrant& grant,
    const LogicalSessionTransport transport,
    const std::string& binding_id,
    const bool takeover,
    const int64_t now_ms) {
    LogicalSessionAdmission result;
    std::string takeover_previous_session_id;
    const auto existing_controller = controller_session_id_;
    if (!existing_controller.empty() && existing_controller != grant.logical_session_id) {
        if (!takeover) {
            result.code = LogicalSessionAdmissionCode::kOccupied;
            return result;
        }
        const auto previous = sessions_.find(existing_controller);
        const bool controller_allows_takeover = previous == sessions_.end()
            || previous->second.allow_takeover;
        if (!allow_takeover_ || !grant.allow_takeover || !controller_allows_takeover) {
            result.code = LogicalSessionAdmissionCode::kTakeoverDisabled;
            return result;
        }
        if (previous != sessions_.end()) {
            takeover_previous_session_id = existing_controller;
            previous->second.role = LogicalSessionRole::kObserver;
            result.previous_controller_lease_generation = previous->second.lease_generation;
            previous->second.lease_generation++;
            previous->second.controller_disconnected_at_ms = 0;
            result.release_previous_controller_input = true;
            result.previous_controller_session_id = existing_controller;
        }
    }

    auto [session_it, inserted] = sessions_.try_emplace(grant.logical_session_id);
    auto& session = session_it->second;
    if (inserted) {
        session.stream_id = grant.stream_id;
        session.subject_id = grant.subject_id;
        session.expires_at_ms = grant.expires_at_ms;
        session.allow_observer = grant.allow_observer;
        session.allow_takeover = grant.allow_takeover;
    } else if (session.stream_id != grant.stream_id || session.subject_id != grant.subject_id) {
        result.code = LogicalSessionAdmissionCode::kInvalidGrant;
        return result;
    }
    const bool renew_controller_lease = controller_session_id_ != grant.logical_session_id
        || !HasControllerBinding(session);
    session.role = LogicalSessionRole::kController;
    if (!takeover_previous_session_id.empty()) {
        session.takeover_previous_session_id = std::move(takeover_previous_session_id);
    }
    if (renew_controller_lease) {
        session.lease_generation++;
    }
    session.controller_disconnected_at_ms = 0;
    session.bindings.insert_or_assign(binding_id, Binding{transport, binding_id});
    controller_session_id_ = grant.logical_session_id;
    result.code = LogicalSessionAdmissionCode::kAccepted;
    result.role = LogicalSessionRole::kController;
    result.lease_generation = session.lease_generation;
    return result;
}

LogicalSessionAdmission LogicalSessionRegistry::Bind(const LogicalSessionGrant& grant,
                                                      const LogicalSessionTransport transport,
                                                      const std::string& binding_id,
                                                      const bool takeover,
                                                      const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    RemoveStaleSessionsLocked(now_ms);
    if (grant.logical_session_id.empty() || grant.stream_id.empty() || grant.subject_id.empty()
        || binding_id.empty() || (grant.expires_at_ms > 0 && now_ms >= grant.expires_at_ms)) {
        return {.code = grant.expires_at_ms > 0 && now_ms >= grant.expires_at_ms
                ? LogicalSessionAdmissionCode::kExpired
                : LogicalSessionAdmissionCode::kInvalidGrant};
    }
    // File transfer is a Controller-scoped reliable capability. It may be the
    // first binding for the standalone file manager and therefore establish a
    // Controller logical session, but its binding is never an input binding.
    if (transport == LogicalSessionTransport::kFileTransfer) {
        const auto session_it = sessions_.find(grant.logical_session_id);
        if (session_it == sessions_.end()) {
            if (grant.join_mode != "control") {
                return {.code = LogicalSessionAdmissionCode::kInvalidGrant};
            }
            return AdoptControllerLocked(grant, transport, binding_id, takeover, now_ms);
        }
        if (session_it->second.stream_id != grant.stream_id
            || session_it->second.subject_id != grant.subject_id
            || session_it->second.role != LogicalSessionRole::kController) {
            return {.code = LogicalSessionAdmissionCode::kInvalidGrant};
        }
        auto& session = session_it->second;
        session.bindings.insert_or_assign(binding_id, Binding{transport, binding_id});
        return {.code = LogicalSessionAdmissionCode::kAccepted,
                .role = session.role,
                .lease_generation = session.lease_generation};
    }
    if (grant.join_mode == "control") {
        return AdoptControllerLocked(grant, transport, binding_id, takeover, now_ms);
    }
    if (grant.join_mode != "observe") {
        return {.code = LogicalSessionAdmissionCode::kInvalidGrant};
    }
    if (!allow_observer_ || !grant.allow_observer) {
        return {.code = LogicalSessionAdmissionCode::kObserversDisabled};
    }
    auto [session_it, inserted] = sessions_.try_emplace(grant.logical_session_id);
    auto& session = session_it->second;
    if (inserted) {
        session.stream_id = grant.stream_id;
        session.subject_id = grant.subject_id;
        session.expires_at_ms = grant.expires_at_ms;
        session.role = LogicalSessionRole::kObserver;
        session.allow_observer = grant.allow_observer;
        session.allow_takeover = grant.allow_takeover;
    } else if (session.stream_id != grant.stream_id || session.subject_id != grant.subject_id) {
        return {.code = LogicalSessionAdmissionCode::kInvalidGrant};
    }
    session.bindings.insert_or_assign(binding_id, Binding{transport, binding_id});
    return {.code = LogicalSessionAdmissionCode::kAccepted,
            .role = LogicalSessionRole::kObserver,
            .lease_generation = session.lease_generation};
}

LogicalSessionBindingClosed LogicalSessionRegistry::CloseBinding(
    const std::string& logical_session_id, const std::string& binding_id, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(logical_session_id);
    if (found == sessions_.end()) {
        return {};
    }
    return CloseBindingLocked(found, binding_id, now_ms);
}

LogicalSessionBindingClosed LogicalSessionRegistry::CloseBindingById(
    const std::string& binding_id, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    for (auto session_it = sessions_.begin(); session_it != sessions_.end(); ++session_it) {
        if (session_it->second.bindings.contains(binding_id)) {
            return CloseBindingLocked(session_it, binding_id, now_ms);
        }
    }
    return {};
}

LogicalSessionBindingClosed LogicalSessionRegistry::CloseBindingLocked(
    const std::unordered_map<std::string, Session>::iterator session_it,
    const std::string& binding_id, const int64_t now_ms) {
    auto& session = session_it->second;
    if (session.bindings.erase(binding_id) == 0) {
        return {};
    }
    LogicalSessionBindingClosed result{.logical_session_id = session_it->first};
    if (session.role == LogicalSessionRole::kController && !HasControllerBinding(session)) {
        session.controller_disconnected_at_ms = now_ms;
        result.release_controller_input = true;
        result.lease_generation = session.lease_generation;
        return result;
    }
    if (session.bindings.empty() && session.role == LogicalSessionRole::kObserver) {
        sessions_.erase(session_it);
        result.logical_session_closed = true;
    }
    return result;
}

bool LogicalSessionRegistry::AuthorizeControllerInput(const std::string& logical_session_id,
                                                       const uint64_t lease_generation,
                                                       const int64_t now_ms) const {
    static_cast<void>(now_ms);
    std::scoped_lock lock(mutex_);
    if (controller_session_id_ != logical_session_id) {
        return false;
    }
    const auto found = sessions_.find(logical_session_id);
    return found != sessions_.end()
        && found->second.role == LogicalSessionRole::kController
        && found->second.lease_generation == lease_generation
        && HasControllerBinding(found->second);
}

bool LogicalSessionRegistry::AuthorizeControllerInputBinding(
    const std::string& binding_id, const int64_t now_ms) const {
    return FindControllerInputLeaseByBinding(binding_id, now_ms).has_value();
}

bool LogicalSessionRegistry::AuthorizeControllerInputStream(
    const std::string& stream_id, const int64_t now_ms) const {
    return FindControllerInputLeaseByStream(stream_id, now_ms).has_value();
}

std::optional<LogicalSessionInputLease> LogicalSessionRegistry::FindControllerInputLeaseByBinding(
    const std::string& binding_id, const int64_t now_ms) const {
    static_cast<void>(now_ms);
    std::scoped_lock lock(mutex_);
    for (const auto& [logical_session_id, session] : sessions_) {
        if (!session.bindings.contains(binding_id)) {
            continue;
        }
        if (controller_session_id_ == logical_session_id
            && session.role == LogicalSessionRole::kController
            && HasControllerBinding(session)) {
            return LogicalSessionInputLease{
                .logical_session_id = logical_session_id,
                .binding_id = binding_id,
                .generation = session.lease_generation,
            };
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<LogicalSessionInputLease> LogicalSessionRegistry::FindControllerLeaseByBinding(
    const std::string& binding_id, const int64_t now_ms) const {
    static_cast<void>(now_ms);
    std::scoped_lock lock(mutex_);
    for (const auto& [logical_session_id, session] : sessions_) {
        if (!session.bindings.contains(binding_id)) {
            continue;
        }
        if (controller_session_id_ == logical_session_id
            && session.role == LogicalSessionRole::kController) {
            return LogicalSessionInputLease{
                .logical_session_id = logical_session_id,
                .binding_id = binding_id,
                .generation = session.lease_generation,
            };
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> LogicalSessionRegistry::FindLogicalSessionIdByBinding(
    const std::string& binding_id, const int64_t now_ms) const {
    static_cast<void>(now_ms);
    std::scoped_lock lock(mutex_);
    for (const auto& [logical_session_id, session] : sessions_) {
        if (session.bindings.contains(binding_id)) {
            return logical_session_id;
        }
    }
    return std::nullopt;
}

std::optional<LogicalSessionInputLease> LogicalSessionRegistry::FindControllerInputLeaseByStream(
    const std::string& stream_id, const int64_t now_ms) const {
    static_cast<void>(now_ms);
    std::scoped_lock lock(mutex_);
    for (const auto& [logical_session_id, session] : sessions_) {
        if (session.stream_id != stream_id) {
            continue;
        }
        if (controller_session_id_ != logical_session_id
            || session.role != LogicalSessionRole::kController
            || !HasControllerBinding(session)) {
            return std::nullopt;
        }
        const auto binding = std::find_if(session.bindings.begin(), session.bindings.end(),
            [](const auto& item) {
                return item.second.transport != LogicalSessionTransport::kFileTransfer;
            });
        if (binding == session.bindings.end()) {
            return std::nullopt;
        }
        return LogicalSessionInputLease{
            .logical_session_id = logical_session_id,
            .binding_id = binding->first,
            .generation = session.lease_generation,
        };
    }
    return std::nullopt;
}

std::optional<LogicalSessionRole> LogicalSessionRegistry::FindRole(
    const std::string& logical_session_id) const {
    std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(logical_session_id);
    if (found == sessions_.end()) {
        return std::nullopt;
    }
    return found->second.role;
}

std::optional<std::string> LogicalSessionRegistry::FindStreamId(
    const std::string& logical_session_id) const {
    std::scoped_lock lock(mutex_);
    const auto found = sessions_.find(logical_session_id);
    if (found == sessions_.end()) {
        return std::nullopt;
    }
    return found->second.stream_id;
}

std::vector<LogicalSessionSnapshot> LogicalSessionRegistry::SnapshotActive(
    const int64_t now_ms) const {
    std::scoped_lock lock(mutex_);
    std::vector<LogicalSessionSnapshot> snapshots;
    snapshots.reserve(sessions_.size());
    for (const auto& [logical_session_id, session] : sessions_) {
        const bool controller_reconnecting = session.role == LogicalSessionRole::kController
            && session.bindings.empty()
            && session.controller_disconnected_at_ms > 0
            && now_ms - session.controller_disconnected_at_ms <= controller_reconnect_grace_ms_;
        if (session.bindings.empty() && !controller_reconnecting) {
            continue;
        }
        LogicalSessionSnapshot snapshot{
            .logical_session_id = logical_session_id,
            .takeover_previous_session_id = session.takeover_previous_session_id,
            .stream_id = session.stream_id,
            .subject_id = session.subject_id,
            .role = session.role,
        };
        snapshot.transports.reserve(session.bindings.size());
        for (const auto& [binding_id, binding] : session.bindings) {
            static_cast<void>(binding_id);
            snapshot.transports.push_back(binding.transport);
        }
        std::sort(snapshot.transports.begin(), snapshot.transports.end());
        snapshot.transports.erase(
            std::unique(snapshot.transports.begin(), snapshot.transports.end()),
            snapshot.transports.end());
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

size_t LogicalSessionRegistry::ActiveSessionCount() const {
    std::scoped_lock lock(mutex_);
    return sessions_.size();
}

size_t LogicalSessionRegistry::ObserverCount() const {
    std::scoped_lock lock(mutex_);
    return static_cast<size_t>(std::count_if(sessions_.begin(), sessions_.end(),
        [](const auto& item) { return item.second.role == LogicalSessionRole::kObserver; }));
}

} // namespace px
