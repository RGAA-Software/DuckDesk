#include "rdp/rdp_display_channel.h"
#include <gtest/gtest.h>

namespace px::rdp {
namespace {
std::shared_ptr<DispClientContext> TestChannel() {
    auto channel = std::make_shared<DispClientContext>();
    channel->SendMonitorLayout = [](DispClientContext*, UINT32 count,
                                    DISPLAY_CONTROL_MONITOR_LAYOUT* layout) -> UINT { // NOLINT(gammaray-raw-pointer-boundary): FreeRDP test ABI.
        EXPECT_EQ(count, 1u);
        EXPECT_EQ(layout->Width, 1920u);
        EXPECT_EQ(layout->Height, 1080u);
        return CHANNEL_RC_OK;
    };
    return channel;
}
} // namespace

TEST(RdpDisplayChannel, EarlyResizeWaitsForCapabilitiesAndCoalesces) {
    DisplayChannel display{};
    display.Request({1024, 640});
    EXPECT_TRUE(display.Flush());
    const auto channel = TestChannel();
    display.Attach(channel);
    display.Request({1920, 1080});
    channel->SendMonitorLayout = nullptr;
    EXPECT_TRUE(display.Flush()); // No send before CAPS, even with a missing transport callback.
    EXPECT_EQ(channel->DisplayControlCaps(channel.get(), 1, 8192, 8192), CHANNEL_RC_OK);
    EXPECT_FALSE(display.Flush()); // Failed sends do not consume the pending request.
    channel->SendMonitorLayout = TestChannel()->SendMonitorLayout;
    EXPECT_TRUE(display.Flush());
    channel->SendMonitorLayout = nullptr;
    EXPECT_TRUE(display.Flush()); // Already sent: no duplicate layout.
}

TEST(RdpDisplayChannel, RepeatedDetachReattachReplaysLatestSizeAndRejectsInvalidCaps) {
    DisplayChannel display{};
    display.Request({1920, 1080});
    for (int cycle{0}; cycle < 16; ++cycle) {
        const auto channel = TestChannel();
        display.Attach(channel);
        EXPECT_EQ(channel->DisplayControlCaps(channel.get(), 0, 8192, 8192), ERROR_INVALID_DATA);
        EXPECT_NE(channel->DisplayControlCaps, nullptr);
        EXPECT_EQ(channel->DisplayControlCaps(channel.get(), 1, 8192, 8192), CHANNEL_RC_OK);
        EXPECT_TRUE(display.Flush());
        display.Attach({});
        EXPECT_FALSE(display.Connected());
        EXPECT_TRUE(display.Flush());
    }
}
} // namespace px::rdp
