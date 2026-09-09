#include "application_text_service.h"

#include <chrono>
#include <vector>

#include "application_text_validation.h"
#include "px_ft_engine/ft_sha256.h"

namespace px {
namespace {
std::int64_t NowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

Message Result(const ApplicationTextSubmit& request, const ApplicationTextOutcome outcome) {
    auto message{Message{}};
    message.set_type(kApplicationTextResult);
    auto& result{*message.mutable_application_text_result()};
    result.set_request_id(request.request_id());
    *result.mutable_target() = request.target();
    result.set_outcome(outcome);
    if (outcome == TEXT_SUBMITTED) {
        result.set_accepted_bytes(static_cast<std::uint32_t>(request.text().size()));
    }
    return message;
}

std::string Digest(const ApplicationTextSubmit& request) {
    const auto serialized{request.SerializeAsString()};
    std::vector<std::uint8_t> bytes{};
    bytes.reserve(serialized.size());
    for (const auto byte : serialized) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    auto hasher{ft::Sha256Hasher{}};
    hasher.Update(bytes);
    return ft::Sha256Bytes(hasher.Finalize());
}
} // namespace

ApplicationTextService::ApplicationTextService(std::string instance, std::shared_ptr<LogicalSessionRegistry> registry, ApplicationTextBackend backend)
    : instance_(std::move(instance)), registry_(std::move(registry)), backend_(std::move(backend)) {}

std::string ApplicationTextService::LeaseKey(const LogicalSessionInputLease& lease) {
    return lease.logical_session_id + ":" + std::to_string(lease.generation) + ":" + std::to_string(lease.input_capability_generation);
}

bool ApplicationTextService::ValidLease(const LogicalSessionInputLease& lease) const {
    return registry_ && registry_->IsCurrentInputBinding(lease, NowMilliseconds());
}

bool ApplicationTextService::ValidTarget(const ApplicationTextTarget& target, const LogicalSessionInputLease& lease) const {
    return target.instance_id() == instance_ && target.lease_generation() == std::to_string(lease.input_capability_generation) &&
           !target.target_generation().empty() && target.target_generation().size() <= 128;
}

ApplicationTextTarget ApplicationTextService::Target(const LogicalSessionInputLease& lease, const std::string& target_generation) const {
    auto target{ApplicationTextTarget{}};
    target.set_instance_id(instance_);
    // The wire lease token is the input-capability epoch. Revoking input must
    // invalidate drafts/commits without rotating the generic file-transfer lease.
    target.set_lease_generation(std::to_string(lease.input_capability_generation));
    target.set_target_generation(target_generation);
    return target;
}

void ApplicationTextService::Handle(Message message, LogicalSessionInputLease lease, const bool reliable, std::function<bool()> alive, Reply reply) {
    if (!reply || !alive || !alive() || !ValidLease(lease)) {
        return;
    }
    {
        std::scoped_lock lock{mutex_};
        if (stopped_) {
            return;
        }
    }
    if (message.type() == kApplicationTextCapabilities) {
        Query(lease, std::move(reply));
    } else if (reliable && message.type() == kApplicationTextBarrier && message.has_application_text_barrier()) {
        Barrier(message.application_text_barrier(), std::move(lease), std::move(alive), std::move(reply));
    } else if (reliable && message.type() == kApplicationTextSubmit && message.has_application_text_submit()) {
        Submit(message.application_text_submit(), std::move(lease), std::move(alive), std::move(reply));
    }
}

void ApplicationTextService::Query(const LogicalSessionInputLease& lease, Reply reply) {
    const auto key{LeaseKey(lease)};
    bool query_backend{false};
    auto message{Message{}};
    message.set_type(kApplicationTextCapabilities);
    auto& capabilities{*message.mutable_application_text_capabilities()};
    capabilities.set_version(1);
    capabilities.set_max_utf8_bytes(kApplicationTextMaxBytes);
    capabilities.set_backend(backend_.kind);
    capabilities.set_final_text_supported(backend_.kind != ApplicationTextCapabilities::UNAVAILABLE);
    capabilities.set_state_hints_supported(backend_.kind != ApplicationTextCapabilities::UNAVAILABLE);
    {
        std::scoped_lock lock{mutex_};
        // Old lease state is no longer executable. Bound memory across takeovers.
        if (stopped_) {
            return;
        }
        std::erase_if(states_, [&key](const auto& entry) { return entry.first != key; });
        auto& state{states_[key]};
        capabilities.set_input_generation(std::to_string(state.generation));
        if (backend_.query && !state.query_pending) {
            state.query_pending = true;
            query_backend = true;
        }
    }
    reply(std::move(message));
    if (!query_backend) {
        return;
    }
    const bool lease_valid{ValidLease(lease)};
    {
        // The metadata reply may stop the service or revoke the binding from
        // inside its callback. Do not schedule work after that callback stops it.
        std::scoped_lock lock{mutex_};
        const auto found{states_.find(key)};
        if (stopped_ || found == states_.end()) {
            return;
        }
        if (!lease_valid) {
            found->second.query_pending = false;
            return;
        }
    }
    backend_.query([weak = weak_from_this(), key, lease, reply = std::move(reply)](ApplicationTextBackendState state) {
        const auto self{weak.lock()};
        if (!self) {
            return;
        }
        {
            std::scoped_lock lock{self->mutex_};
            const auto found{self->states_.find(key)};
            if (self->stopped_ || found == self->states_.end()) {
                return;
            }
            found->second.query_pending = false;
        }
        if (!self->ValidLease(lease)) {
            return;
        }
        auto message{Message{}};
        message.set_type(kApplicationTextState);
        auto& value{*message.mutable_application_text_state()};
        *value.mutable_target() = self->Target(lease, state.generation);
        value.set_editability(state.available ? state.editability : ApplicationTextState::NOT_EDITABLE);
        value.set_source(self->backend_.kind == ApplicationTextCapabilities::CEF_COMMIT ? ApplicationTextState::CEF : ApplicationTextState::IMM);
        reply(std::move(message));
    });
}

bool ApplicationTextService::AllowsOrdinaryInput(const LogicalSessionInputLease& lease, const std::string& generation) const {
    std::scoped_lock lock{mutex_};
    if (stopped_) {
        return false;
    }
    const auto found{states_.find(LeaseKey(lease))};
    if (found == states_.end()) {
        return generation.empty() || generation == "0";
    }
    const auto& state{found->second};
    return !state.editing && !state.transition && ((state.generation == 0 && generation.empty()) || generation == std::to_string(state.generation));
}

void ApplicationTextService::Barrier(ApplicationTextBarrier request, LogicalSessionInputLease lease, std::function<bool()> alive, Reply reply) {
    auto reply_barrier{[request, reply](ApplicationTextOutcome outcome, std::uint64_t generation, bool editing) {
        auto message{Message{}};
        message.set_type(kApplicationTextBarrierResult);
        auto& result{*message.mutable_application_text_barrier_result()};
        result.set_request_id(request.request_id());
        *result.mutable_target() = request.target();
        result.set_outcome(outcome);
        result.set_input_generation(std::to_string(generation));
        result.set_editing(editing);
        reply(std::move(message));
    }};
    if (!ValidApplicationTextRequestId(request.request_id()) || !ValidTarget(request.target(), lease) || !backend_.query || !backend_.release_keys) {
        reply_barrier(TEXT_TARGET_CHANGED, 0, false);
        return;
    }
    const auto key{LeaseKey(lease)};
    auto rejection{TEXT_OUTCOME_UNSPECIFIED};
    std::uint64_t generation{};
    bool editing{};
    {
        std::scoped_lock lock{mutex_};
        if (stopped_) {
            return;
        }
        auto& state{states_[key]};
        generation = state.generation;
        editing = state.editing;
        if (state.pending || state.transition) {
            rejection = TEXT_BUSY;
        } else if (request.expected_input_generation() != std::to_string(state.generation)) {
            rejection = TEXT_TARGET_CHANGED;
        } else {
            state.transition = true;
        }
    }
    if (rejection != TEXT_OUTCOME_UNSPECIFIED) {
        reply_barrier(rejection, generation, editing);
        return;
    }
    backend_.query([weak = weak_from_this(), key, request, lease, alive, reply_barrier](ApplicationTextBackendState target) {
        const auto self{weak.lock()};
        if (!self) {
            return;
        }
        auto outcome{TEXT_SUBMITTED};
        if (!alive() || !self->ValidLease(lease)) {
            outcome = TEXT_PERMISSION_DENIED;
        } else if (request.begin_editing() && (target.generation != request.target().target_generation() || !target.available)) {
            outcome = TEXT_TARGET_CHANGED;
        }
        std::uint64_t next_generation{};
        bool current_editing{};
        {
            std::scoped_lock lock{self->mutex_};
            const auto found{self->states_.find(key)};
            if (self->stopped_ || found == self->states_.end()) {
                return;
            }
            auto& state{found->second};
            if (outcome == TEXT_SUBMITTED) {
                ++state.generation;
                state.editing = request.begin_editing();
            } else {
                state.transition = false;
            }
            next_generation = state.generation;
            current_editing = state.editing;
        }
        if (outcome != TEXT_SUBMITTED) {
            reply_barrier(outcome, next_generation, current_editing);
            return;
        }
        self->backend_.release_keys([weak, key, lease, alive, reply_barrier, next_generation, current_editing](bool released) {
            const auto self{weak.lock()};
            if (!self) {
                return;
            }
            const auto outcome{!released ? TEXT_OUTCOME_UNKNOWN : alive() && self->ValidLease(lease) ? TEXT_SUBMITTED : TEXT_PERMISSION_DENIED};
            {
                std::scoped_lock lock{self->mutex_};
                const auto found{self->states_.find(key)};
                if (self->stopped_ || found == self->states_.end() || found->second.generation != next_generation) {
                    return;
                }
                found->second.transition = !released;
            }
            reply_barrier(outcome, next_generation, current_editing);
        });
    });
}

void ApplicationTextService::Submit(ApplicationTextSubmit request, LogicalSessionInputLease lease, std::function<bool()> alive, Reply reply) {
    if (!ValidApplicationTextRequestId(request.request_id()) || !ValidApplicationText(request.text())) {
        reply(Result(request, TEXT_INVALID));
        return;
    }
    if (!ValidTarget(request.target(), lease) || !backend_.commit) {
        reply(Result(request, TEXT_TARGET_CHANGED));
        return;
    }
    const auto key{LeaseKey(lease)};
    const auto digest{Digest(request)};
    auto outcome{TEXT_ACCEPTED};
    bool execute{false};
    {
        std::scoped_lock lock{mutex_};
        if (stopped_) {
            return;
        }
        auto& state{states_[key]};
        const auto found{state.requests.find(request.request_id())};
        if (found != state.requests.end()) {
            outcome = found->second.digest == digest ? found->second.outcome : TEXT_INVALID;
        } else if (!state.editing || request.input_generation() != std::to_string(state.generation)) {
            outcome = TEXT_TARGET_CHANGED;
        } else if (state.transition || state.pending || state.requests.size() >= 4096) {
            outcome = TEXT_BUSY;
        } else {
            state.requests.emplace(request.request_id(), Entry{digest, TEXT_ACCEPTED, request.target()});
            state.pending = true;
            execute = true;
        }
    }
    reply(Result(request, outcome));
    if (!execute) {
        return;
    }
    const auto authorize{[weak = weak_from_this(), key, lease, alive, generation = request.input_generation()] {
        const auto self{weak.lock()};
        if (!self || !alive() || !self->ValidLease(lease)) {
            return false;
        }
        std::scoped_lock lock{self->mutex_};
        const auto found{self->states_.find(key)};
        return !self->stopped_ && found != self->states_.end() && found->second.editing && !found->second.transition &&
               std::to_string(found->second.generation) == generation;
    }};
    backend_.commit(request.text(), request.target().target_generation(), authorize,
                    [weak = weak_from_this(), key, request, reply](ApplicationTextOutcome outcome) {
                        const auto self{weak.lock()};
                        if (!self) {
                            return;
                        }
                        {
                            std::scoped_lock lock{self->mutex_};
                            const auto found{self->states_.find(key)};
                            if (self->stopped_ || found == self->states_.end()) {
                                return;
                            }
                            found->second.pending = false;
                            found->second.requests.at(request.request_id()).outcome = outcome;
                        }
                        reply(Result(request, outcome));
                    });
}

void ApplicationTextService::Stop() {
    std::scoped_lock lock{mutex_};
    stopped_ = true;
    states_.clear();
}
} // namespace px
