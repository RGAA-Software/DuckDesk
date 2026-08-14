//
// Echo / suppress helpers for bidirectional clipboard sync.
//

#ifndef TC_COMMON_NEW_CLIPBOARD_ECHO_H
#define TC_COMMON_NEW_CLIPBOARD_ECHO_H

#include <atomic>
#include <mutex>
#include <string>

namespace tc::clipboard
{

    class EchoFilter {
    public:
        void SetRemoteEcho(const std::string& text);
        std::string GetRemoteEcho() const;

        void BeginSuppressOutbound();
        void EndSuppressOutbound();
        bool IsOutboundSuppressed() const;

        // True when this local change should not be synced outbound (remote echo).
        bool ShouldSkipOutbound(const std::string& local_text) const;

    private:
        mutable std::mutex mutex_;
        std::string remote_echo_;
        std::atomic<int> suppress_outbound_{0};
    };

    class SuppressOutboundGuard {
    public:
        explicit SuppressOutboundGuard(EchoFilter& filter) : filter_(filter) {
            filter_.BeginSuppressOutbound();
        }
        ~SuppressOutboundGuard() {
            filter_.EndSuppressOutbound();
        }
    private:
        EchoFilter& filter_;
    };

}

#endif
