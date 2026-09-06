#include "direct_session_grant_store.h"

#include "px_common/uuid.h"

#include <utility>

namespace px {

void DirectSessionGrantStore::RemoveExpiredLocked(const int64_t now_ms) {
    for (auto it = grants_.begin(); it != grants_.end();) {
        if (now_ms >= it->second.expires_at_ms_) {
            it = grants_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string DirectSessionGrantStore::Issue(const DirectSessionGrantBinding& binding, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    RemoveExpiredLocked(now_ms);
    auto token = GetUUID();
    grants_.insert_or_assign(token, GrantRecord{
                                        .binding_ = binding,
                                        .expires_at_ms_ = now_ms + kLifetimeMilliseconds,
                                    });
    return token;
}

std::string DirectSessionGrantStore::IssueStreamBinding(DirectSessionGrantBinding binding, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    RemoveExpiredLocked(now_ms);
    // The stream id is used in URL query strings by the local Panel and
    // Render signaling endpoints. Use a URL-safe hexadecimal identity so
    // '+' can never be decoded as a space by form-style query parsers.
    const auto stream_id = std::string("ip-direct:") + GetUUIDInMD5();
    binding.stream_id_ = stream_id;
    grants_.insert_or_assign(stream_id, GrantRecord{
                                            .binding_ = std::move(binding),
                                            .expires_at_ms_ = now_ms + kLifetimeMilliseconds,
                                        });
    return stream_id;
}

bool DirectSessionGrantStore::Redeem(const std::string& token, const DirectSessionGrantBinding& expected, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    RemoveExpiredLocked(now_ms);
    const auto found = grants_.find(token);
    if (found == grants_.end() || found->second.binding_.device_id_ != expected.device_id_ ||
        found->second.binding_.stream_id_ != expected.stream_id_ || found->second.binding_.client_nonce_ != expected.client_nonce_ ||
        found->second.binding_.remote_address_ != expected.remote_address_) {
        return false;
    }
    grants_.erase(found);
    return true;
}

bool DirectSessionGrantStore::Validate(const std::string& token, const DirectSessionGrantBinding& expected, const int64_t now_ms) {
    std::scoped_lock lock(mutex_);
    RemoveExpiredLocked(now_ms);
    const auto found = grants_.find(token);
    return found != grants_.end() && found->second.binding_.device_id_ == expected.device_id_ &&
           found->second.binding_.stream_id_ == expected.stream_id_ && found->second.binding_.client_nonce_ == expected.client_nonce_ &&
           found->second.binding_.remote_address_ == expected.remote_address_;
}

} // namespace px
