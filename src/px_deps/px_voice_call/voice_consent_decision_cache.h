#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace px {

struct VoiceConsentDecision {
    std::string call_id;
    uint64_t request_id = 0;
    bool accepted = false;
    std::string reason;
    uint64_t expires_at_ms = 0;
};

// Small, bounded replay cache used after a consent request has left the
// state machine. It prevents a retransmitted old request from displaying a
// second prompt or reopening audio resources.
class VoiceConsentDecisionCache {
public:
    static constexpr uint64_t kDefaultTtlMs = 5 * 60 * 1000;
    static constexpr size_t kDefaultCapacity = 64;

    explicit VoiceConsentDecisionCache(
        uint64_t ttl_ms = kDefaultTtlMs,
        size_t capacity = kDefaultCapacity);

    void Put(
        const std::string& call_id, uint64_t request_id, bool accepted,
        std::string reason, uint64_t now_ms);
    [[nodiscard]] std::optional<VoiceConsentDecision> Find(
        const std::string& call_id, uint64_t request_id, uint64_t now_ms);
    [[nodiscard]] size_t Size(uint64_t now_ms);

private:
    void Prune(uint64_t now_ms);

    uint64_t ttl_ms_;
    size_t capacity_;
    std::deque<VoiceConsentDecision> entries_;
};

}  // namespace px
