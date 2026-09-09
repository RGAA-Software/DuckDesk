#pragma once

#include <freerdp/client/disp.h>
#include <QSize>
#include <memory>
#include <optional>

namespace px::rdp {

// Protocol-thread-only adapter. ChannelConnected precedes the server CAPS PDU.
// The one-shot ABI callback clears itself when CAPS arrives; no owner/context
// pointer or global callback registry is needed to communicate readiness.
class DisplayChannel final {
  public:
    void Attach(std::shared_ptr<DispClientContext> channel) {
        channel_ = std::move(channel);
        sent_.reset();
        if (channel_) {
            channel_->DisplayControlCaps = Capabilities;
        }
    }
    void Request(QSize size) {
        requested_ = size;
    }
    bool Flush() {
        if (!channel_ || channel_->DisplayControlCaps || !requested_ || requested_ == sent_) {
            return true;
        }
        DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
        monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
        monitor.Width = static_cast<UINT32>(requested_->width()) & ~1u;
        monitor.Height = static_cast<UINT32>(requested_->height());
        monitor.PhysicalWidth = 300;
        monitor.PhysicalHeight = 200;
        monitor.DesktopScaleFactor = 100;
        monitor.DeviceScaleFactor = 100;
        if (!channel_->SendMonitorLayout || channel_->SendMonitorLayout(channel_.get(), 1, &monitor) != CHANNEL_RC_OK) {
            return false;
        }
        sent_ = requested_;
        return true;
    }
    bool Connected() const noexcept {
        return static_cast<bool>(channel_);
    }

  private:
    // FreeRDP synchronous callback ABI, called only on the protocol thread.
    static UINT Capabilities(DispClientContext* channel, UINT32 n, UINT32 a, UINT32 b) noexcept { // NOLINT(gammaray-raw-pointer-boundary)
        if (!channel || n == 0 || a == 0 || b == 0) {
            return ERROR_INVALID_DATA;
        }
        channel->DisplayControlCaps = nullptr;
        return CHANNEL_RC_OK;
    }
    std::shared_ptr<DispClientContext> channel_{};
    std::optional<QSize> requested_{};
    std::optional<QSize> sent_{};
};

} // namespace px::rdp
