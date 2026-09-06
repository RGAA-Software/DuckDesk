#include <gtest/gtest.h>

#include "px_render/network/webrtc/local/rtc_heartbeat_watchdog.h"

namespace px {

TEST(RtcHeartbeatWatchdog, RemainsDisabledUntilTheSessionSupportsApplicationHeartbeats) {
    RtcHeartbeatWatchdog watchdog(10000);

    EXPECT_FALSE(watchdog.IsArmed());
    EXPECT_FALSE(watchdog.HasExpired(60000));
}

TEST(RtcHeartbeatWatchdog, ExpiresAtTheConfiguredDeadline) {
    RtcHeartbeatWatchdog watchdog(10000);
    watchdog.Arm(1000);

    EXPECT_TRUE(watchdog.IsArmed());
    EXPECT_FALSE(watchdog.HasExpired(10999));
    EXPECT_TRUE(watchdog.HasExpired(11000));
}

TEST(RtcHeartbeatWatchdog, AHeartbeatMovesTheDeadlineForward) {
    RtcHeartbeatWatchdog watchdog(10000);
    watchdog.Arm(1000);
    watchdog.ObserveHeartbeat(9000);

    EXPECT_FALSE(watchdog.HasExpired(11000));
    EXPECT_FALSE(watchdog.HasExpired(18999));
    EXPECT_TRUE(watchdog.HasExpired(19000));
}

TEST(RtcHeartbeatWatchdog, ResetDisarmsAReusedSession) {
    RtcHeartbeatWatchdog watchdog(10000);
    watchdog.Arm(1000);
    watchdog.Reset();

    EXPECT_FALSE(watchdog.IsArmed());
    EXPECT_FALSE(watchdog.HasExpired(60000));
}

} // namespace px
