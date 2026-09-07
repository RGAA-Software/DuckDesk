//
// UDP-direct media fallback state. Kept independent from socket ownership so
// timeout, UDP receive and shutdown callbacks can race without starting more
// than one WebSocket fallback.
//

#ifndef GAMMARAYPC_UDP_MEDIA_FALLBACK_STATE_H
#define GAMMARAYPC_UDP_MEDIA_FALLBACK_STATE_H

#include <atomic>
#include <cstdint>

namespace px {

    enum class UdpMediaTransport : uint8_t {
        kInactive,
        kProbing,
        kUdpActive,
        kFallbackConnecting,
        kWebSocketFallback,
        kStopped,
    };

    class UdpMediaFallbackState final {
    public:
        void BeginProbe() {
            transport_.store(UdpMediaTransport::kProbing, std::memory_order_release);
        }

        void Stop() {
            transport_.store(UdpMediaTransport::kStopped, std::memory_order_release);
        }

        [[nodiscard]] UdpMediaTransport Current() const {
            return transport_.load(std::memory_order_acquire);
        }

        // A complete video frame or an ordered audio frame proves that UDP is
        // usable. A late UDP callback after fallback has begun is ignored.
        bool MarkUdpMediaReady() {
            auto expected = UdpMediaTransport::kProbing;
            return transport_.compare_exchange_strong(
                expected, UdpMediaTransport::kUdpActive,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        // Both probe timeout and the UDP watchdog may request fallback. Exactly
        // one caller wins; all later requests are no-ops.
        bool BeginFallback() {
            auto current = transport_.load(std::memory_order_acquire);
            while (current == UdpMediaTransport::kProbing ||
                   current == UdpMediaTransport::kUdpActive) {
                if (transport_.compare_exchange_weak(
                        current, UdpMediaTransport::kFallbackConnecting,
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                    return true;
                }
            }
            return false;
        }

        void MarkWebSocketFallbackActive() {
            transport_.store(UdpMediaTransport::kWebSocketFallback, std::memory_order_release);
        }

        [[nodiscard]] bool UsesUdpMedia() const {
            const auto current = Current();
            return current == UdpMediaTransport::kProbing ||
                   current == UdpMediaTransport::kUdpActive;
        }

    private:
        std::atomic<UdpMediaTransport> transport_{UdpMediaTransport::kInactive};
    };

} // namespace px

#endif // GAMMARAYPC_UDP_MEDIA_FALLBACK_STATE_H
