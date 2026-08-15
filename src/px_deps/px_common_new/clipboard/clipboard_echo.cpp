#include "clipboard_echo.h"

namespace tc::clipboard
{

    void EchoFilter::SetRemoteEcho(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        remote_echo_ = text;
    }

    std::string EchoFilter::GetRemoteEcho() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return remote_echo_;
    }

    void EchoFilter::BeginSuppressOutbound() {
        suppress_outbound_.fetch_add(1, std::memory_order_relaxed);
    }

    void EchoFilter::EndSuppressOutbound() {
        suppress_outbound_.fetch_sub(1, std::memory_order_relaxed);
    }

    bool EchoFilter::IsOutboundSuppressed() const {
        return suppress_outbound_.load(std::memory_order_relaxed) > 0;
    }

    bool EchoFilter::ShouldSkipOutbound(const std::string& local_text) const {
        if (IsOutboundSuppressed()) {
            return true;
        }
        return local_text == GetRemoteEcho();
    }

}
