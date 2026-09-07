#ifndef PX_DIRECT_RTC_FALLBACK_STATE_H
#define PX_DIRECT_RTC_FALLBACK_STATE_H

namespace px
{

    // RunningStreamManager serializes access with running_mutex_. A local Panel
    // websocket is intentionally not sufficient to close this gate: only the
    // remote transport-ready signal proves that Direct RTC actually connected.
    class DirectRtcFallbackState final {
    public:
        void MarkPanelChannelConnected() noexcept {
            panel_channel_connected_ = true;
        }

        void MarkTransportConnected() noexcept {
            transport_connected_ = true;
        }

        void MarkTerminalRejected() noexcept {
            terminal_rejected_ = true;
        }

        [[nodiscard]] bool ShouldFallback() const noexcept {
            return !transport_connected_ && !terminal_rejected_;
        }

        [[nodiscard]] bool IsPanelChannelConnected() const noexcept {
            return panel_channel_connected_;
        }

    private:
        bool panel_channel_connected_ = false;
        bool transport_connected_ = false;
        bool terminal_rejected_ = false;
    };

}

#endif // PX_DIRECT_RTC_FALLBACK_STATE_H
