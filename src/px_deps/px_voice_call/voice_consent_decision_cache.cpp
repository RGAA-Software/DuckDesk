#include "voice_consent_decision_cache.h"

#include <algorithm>

namespace px {

VoiceConsentDecisionCache::VoiceConsentDecisionCache(
    uint64_t ttl_ms, size_t capacity)
    : ttl_ms_(ttl_ms == 0 ? 1 : ttl_ms),
      capacity_(capacity == 0 ? 1 : capacity) {}

void VoiceConsentDecisionCache::Put(
    const std::string& call_id, uint64_t request_id, bool accepted,
    std::string reason, uint64_t now_ms) {
    if (call_id.empty() || request_id == 0) {
        return;
    }
    Prune(now_ms);
    for (auto& entry : entries_) {
        if (entry.call_id == call_id && entry.request_id == request_id) {
            entry.accepted = accepted;
            entry.reason = std::move(reason);
            entry.expires_at_ms = now_ms + ttl_ms_;
            return;
        }
    }
    if (entries_.size() >= capacity_) {
        entries_.pop_front();
    }
    entries_.push_back(VoiceConsentDecision{
        .call_id = call_id,
        .request_id = request_id,
        .accepted = accepted,
        .reason = std::move(reason),
        .expires_at_ms = now_ms + ttl_ms_,
    });
}

std::optional<VoiceConsentDecision> VoiceConsentDecisionCache::Find(
    const std::string& call_id, uint64_t request_id, uint64_t now_ms) {
    Prune(now_ms);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->call_id == call_id && it->request_id == request_id) {
            return *it;
        }
    }
    return std::nullopt;
}

size_t VoiceConsentDecisionCache::Size(uint64_t now_ms) {
    Prune(now_ms);
    return entries_.size();
}

void VoiceConsentDecisionCache::Prune(uint64_t now_ms) {
    std::erase_if(entries_, [now_ms](const VoiceConsentDecision& entry) {
        return entry.expires_at_ms <= now_ms;
    });
}

}  // namespace px
