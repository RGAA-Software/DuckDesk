#include <gtest/gtest.h>

#include <memory>

#include "px_render/app/application_idle_lifecycle.h"

namespace px {
namespace {

using namespace std::chrono_literals;
const ApplicationIdleLifecycle::Clock::time_point kStartedAt{};

TEST(ApplicationIdleLifecycle, NeverConnectedInstanceExitsAfterFortyFiveSecondsOnlyOnce) {
    ApplicationIdleLifecycle lifecycle{};
    const auto deadline = lifecycle.ArmStartup(kStartedAt);
    ASSERT_TRUE(deadline);
    EXPECT_TRUE(deadline->startup);
    EXPECT_FALSE(lifecycle.ArmStartup(kStartedAt + 10s));
    EXPECT_FALSE(lifecycle.ClaimExpiry(*deadline, kStartedAt + 44999ms));
    EXPECT_TRUE(lifecycle.ClaimExpiry(*deadline, kStartedAt + 45s));
    EXPECT_FALSE(lifecycle.ClaimExpiry(*deadline, kStartedAt + 46s));
}

TEST(ApplicationIdleLifecycle, EarlyDisconnectUsesTenSecondsInsteadOfStartupGrace) {
    ApplicationIdleLifecycle lifecycle{};
    const auto startup = lifecycle.ArmStartup(kStartedAt);
    lifecycle.Connected("first-viewer");
    const auto disconnect = lifecycle.Disconnected("first-viewer", kStartedAt + 2s);
    ASSERT_TRUE(disconnect);
    EXPECT_FALSE(disconnect->startup);
    EXPECT_FALSE(lifecycle.ClaimExpiry(*disconnect, kStartedAt + 11999ms));
    EXPECT_TRUE(lifecycle.ClaimExpiry(*disconnect, kStartedAt + 12s));
    EXPECT_FALSE(lifecycle.ClaimExpiry(*startup, kStartedAt + 45s));
}

TEST(ApplicationIdleLifecycle, CustomGraceKeepsStartupTimeoutAndReconnectCancellation) {
    ApplicationIdleLifecycle lifecycle{30s};
    const auto startup = lifecycle.ArmStartup(kStartedAt);
    ASSERT_TRUE(startup);
    EXPECT_EQ(startup->expires_at, kStartedAt + 45s);
    lifecycle.Connected("viewer");
    const auto first_disconnect = lifecycle.Disconnected("viewer", kStartedAt + 1s);
    ASSERT_TRUE(first_disconnect);
    EXPECT_EQ(first_disconnect->expires_at, kStartedAt + 31s);
    EXPECT_FALSE(lifecycle.ClaimExpiry(*first_disconnect, kStartedAt + 30999ms));
    lifecycle.Connected("viewer");
    EXPECT_FALSE(lifecycle.ClaimExpiry(*first_disconnect, kStartedAt + 31s));
    const auto second_disconnect = lifecycle.Disconnected("viewer", kStartedAt + 40s);
    ASSERT_TRUE(second_disconnect);
    EXPECT_FALSE(lifecycle.ClaimExpiry(*second_disconnect, kStartedAt + 69999ms));
    EXPECT_TRUE(lifecycle.ClaimExpiry(*second_disconnect, kStartedAt + 70s));
}

TEST(ApplicationIdleLifecycle, ReconnectInvalidatesEveryPreviousDeadline) {
    ApplicationIdleLifecycle lifecycle{};
    const auto startup = lifecycle.ArmStartup(kStartedAt);
    for (int visit_index{}; visit_index < 3; ++visit_index) {
        lifecycle.Connected("viewer");
        const auto disconnect = lifecycle.Disconnected("viewer", kStartedAt + 10s * visit_index);
        ASSERT_TRUE(disconnect);
        lifecycle.Connected("viewer");
        EXPECT_FALSE(lifecycle.ClaimExpiry(*disconnect, kStartedAt + 1h));
        EXPECT_FALSE(lifecycle.ClaimExpiry(*startup, kStartedAt + 1h));
        EXPECT_TRUE(lifecycle.HasClients());
    }
    const auto final_disconnect = lifecycle.Disconnected("viewer", kStartedAt + 1h);
    ASSERT_TRUE(final_disconnect);
    EXPECT_TRUE(lifecycle.ClaimExpiry(*final_disconnect, kStartedAt + 1h + 10s));
}

TEST(ApplicationIdleLifecycle, RemainingConnectionsPreventExitAndDuplicatesDoNotExtendGrace) {
    ApplicationIdleLifecycle lifecycle{};
    const auto startup = lifecycle.ArmStartup(kStartedAt);
    lifecycle.Connected("controller");
    lifecycle.Connected("observer");
    lifecycle.Connected("observer");
    EXPECT_FALSE(lifecycle.Disconnected("controller", kStartedAt + 3s));
    EXPECT_TRUE(lifecycle.HasClients());
    EXPECT_FALSE(lifecycle.Disconnected("stale-connection", kStartedAt + 3s));
    EXPECT_FALSE(lifecycle.ClaimExpiry(*startup, kStartedAt + 1h));
    const auto disconnect = lifecycle.Disconnected("observer", kStartedAt + 1h);
    ASSERT_TRUE(disconnect);
    EXPECT_FALSE(lifecycle.Disconnected("observer", kStartedAt + 1h + 4s));
    EXPECT_TRUE(lifecycle.ClaimExpiry(*disconnect, kStartedAt + 1h + 10s));
}

TEST(ApplicationIdleLifecycle, EmptyAndUnknownConnectionsCannotCancelStartupTimeout) {
    ApplicationIdleLifecycle lifecycle{};
    const auto startup = lifecycle.ArmStartup(kStartedAt);
    lifecycle.Connected("");
    EXPECT_FALSE(lifecycle.Disconnected("", kStartedAt + 1s));
    EXPECT_FALSE(lifecycle.Disconnected("setup-probe", kStartedAt + 1s));
    EXPECT_TRUE(lifecycle.ClaimExpiry(*startup, kStartedAt + 45s));
}

TEST(ApplicationIdleLifecycle, ConnectionsBeforeStartupArmingArePreserved) {
    ApplicationIdleLifecycle lifecycle{};
    lifecycle.Connected("early-viewer");
    EXPECT_FALSE(lifecycle.ArmStartup(kStartedAt));
    const auto disconnect = lifecycle.Disconnected("early-viewer", kStartedAt + 1s);
    ASSERT_TRUE(disconnect);
    EXPECT_TRUE(lifecycle.ClaimExpiry(*disconnect, kStartedAt + 11s));
}

TEST(ApplicationIdleLifecycle, DisconnectBeforeArmingReceivesDisconnectGrace) {
    ApplicationIdleLifecycle lifecycle{};
    lifecycle.Connected("early-viewer");
    EXPECT_FALSE(lifecycle.Disconnected("early-viewer", kStartedAt));
    const auto deadline = lifecycle.ArmStartup(kStartedAt + 1s);
    ASSERT_TRUE(deadline);
    EXPECT_FALSE(deadline->startup);
    EXPECT_TRUE(lifecycle.ClaimExpiry(*deadline, kStartedAt + 11s));
}

TEST(ApplicationIdleLifecycle, ShutdownFromCallbackInvalidatesQueuedExpiryAndRepeatedStopIsSafe) {
    const auto lifecycle = std::make_shared<ApplicationIdleLifecycle>();
    const auto deadline = lifecycle->ArmStartup(kStartedAt);
    const std::weak_ptr<ApplicationIdleLifecycle> weak_lifecycle{lifecycle};
    const auto shutdown = [weak_lifecycle] {
        if (const auto active_lifecycle = weak_lifecycle.lock()) active_lifecycle->Stop();
    };
    shutdown();
    shutdown();
    lifecycle->Connected("late-viewer");
    EXPECT_FALSE(lifecycle->HasClients());
    EXPECT_FALSE(lifecycle->ClaimExpiry(*deadline, kStartedAt + 1h));
    EXPECT_FALSE(lifecycle->ArmStartup(kStartedAt + 1h));
}

TEST(ApplicationIdleLifecycle, QueuedCallbackDoesNotKeepDestroyedLifecycleAlive) {
    auto lifecycle = std::make_shared<ApplicationIdleLifecycle>();
    const auto deadline = lifecycle->ArmStartup(kStartedAt);
    const std::weak_ptr<ApplicationIdleLifecycle> weak_lifecycle{lifecycle};
    const auto callback = [weak_lifecycle, deadline] {
        const auto active_lifecycle = weak_lifecycle.lock();
        return active_lifecycle && active_lifecycle->ClaimExpiry(*deadline, kStartedAt + 45s);
    };
    lifecycle.reset();
    EXPECT_FALSE(callback());
    EXPECT_TRUE(weak_lifecycle.expired());
}

}  // namespace
}  // namespace px
