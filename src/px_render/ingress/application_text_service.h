#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "px_message.pb.h"
#include "session/logical_session_registry.h"

namespace px {

struct ApplicationTextBackendState {
    std::string generation{};
    ApplicationTextState::Editability editability{ApplicationTextState::UNKNOWN};
    bool available{false};
};

struct ApplicationTextBackend {
    ApplicationTextCapabilities::Backend kind{ApplicationTextCapabilities::UNAVAILABLE};
    std::function<void(std::function<void(ApplicationTextBackendState)>)> query{};
    std::function<void(std::function<void(bool)>)> release_keys{};
    std::function<void(std::string, std::string, std::function<bool()>, std::function<void(ApplicationTextOutcome)>)> commit{};
};

// One application, one controller lease. Transport owns reply/alive closures;
// neither auxiliary sockets nor this service own Windows sessions or occupancy.
class ApplicationTextService final : public std::enable_shared_from_this<ApplicationTextService> {
  public:
    using Reply = std::function<void(Message)>;
    ApplicationTextService(std::string instance, std::shared_ptr<LogicalSessionRegistry> registry, ApplicationTextBackend backend);
    void Handle(Message message, LogicalSessionInputLease lease, bool reliable, std::function<bool()> alive, Reply reply);
    bool AllowsOrdinaryInput(const LogicalSessionInputLease& lease, const std::string& generation) const;
    void Stop();

  private:
    struct Entry {
        std::string digest{};
        ApplicationTextOutcome outcome{TEXT_ACCEPTED};
        ApplicationTextTarget target{};
    };
    struct State {
        std::uint64_t generation{0};
        bool editing{false};
        bool transition{false};
        bool pending{false};
        bool query_pending{false};
        std::unordered_map<std::string, Entry> requests{};
    };
    static std::string LeaseKey(const LogicalSessionInputLease& lease);
    bool ValidLease(const LogicalSessionInputLease& lease) const;
    bool ValidTarget(const ApplicationTextTarget& target, const LogicalSessionInputLease& lease) const;
    void Query(const LogicalSessionInputLease& lease, Reply reply);
    void Barrier(ApplicationTextBarrier request, LogicalSessionInputLease lease, std::function<bool()> alive, Reply reply);
    void Submit(ApplicationTextSubmit request, LogicalSessionInputLease lease, std::function<bool()> alive, Reply reply);
    ApplicationTextTarget Target(const LogicalSessionInputLease& lease, const std::string& target_generation) const;

    const std::string instance_{};
    const std::shared_ptr<LogicalSessionRegistry> registry_{};
    const ApplicationTextBackend backend_{};
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, State> states_{};
    bool stopped_{false};
};

} // namespace px
