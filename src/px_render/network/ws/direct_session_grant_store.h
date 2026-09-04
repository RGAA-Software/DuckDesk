#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace px {

    struct DirectSessionGrantBinding {
        std::string device_id_;
        std::string stream_id_;
        std::string client_nonce_;
        std::string remote_address_;
    };

    // In-memory, single-use grants for the no-Console Direct RTC route.  A
    // grant is bound to one peer/session identity and is consumed before the
    // replacement grant is returned, so a captured value cannot be replayed.
    class DirectSessionGrantStore {
    public:
        [[nodiscard]] std::string Issue(const DirectSessionGrantBinding& binding,
                                        int64_t now_ms);
        // Creates the normal high-entropy stream id for a password-validated
        // IP-direct launch and reserves that id as the one-time store key.
        [[nodiscard]] std::string IssueStreamBinding(DirectSessionGrantBinding binding,
                                                     int64_t now_ms);
        [[nodiscard]] bool Redeem(const std::string& token,
                                  const DirectSessionGrantBinding& expected,
                                  int64_t now_ms);

        static constexpr int64_t kLifetimeMilliseconds = 5 * 60 * 1000;

    private:
        struct GrantRecord {
            DirectSessionGrantBinding binding_;
            int64_t expires_at_ms_ = 0;
        };

        void RemoveExpiredLocked(int64_t now_ms);

        std::mutex mutex_;
        std::unordered_map<std::string, GrantRecord> grants_;
    };

} // namespace px
