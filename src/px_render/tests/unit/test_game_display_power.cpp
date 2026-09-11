#include <gtest/gtest.h>
#include "app/win/game_display_power.h"

TEST(GameDisplayPower, RepeatedAcquireStopAndDestroy) {
    for (int iteration{}; iteration < 5; ++iteration) {
        auto lease = px::GameDisplayPower::Acquire();
        ASSERT_TRUE(lease);
        EXPECT_TRUE(lease->Active());
        lease->Stop();
        EXPECT_FALSE(lease->Active());
        lease->Stop();
    }
}

TEST(GameDisplayPower, IndependentOwnersAndDestructionWithoutStop) {
    auto first = px::GameDisplayPower::Acquire();
    auto second = px::GameDisplayPower::Acquire();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    first.reset();
    EXPECT_TRUE(second->Active());
    auto moved = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_TRUE(moved->Active());
}
