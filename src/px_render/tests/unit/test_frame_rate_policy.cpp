#include "app/frame_rate_policy.h"
#include <gtest/gtest.h>

using px::render::FrameRateAdmission;
using namespace std::chrono_literals;

TEST(FrameRatePolicy, HookSixtyInputObeysThirtyTarget) {
    FrameRateAdmission gate{};
    int accepted{};
    for (int index{}; index < 600; ++index)
        accepted += gate.Admit(FrameRateAdmission::Clock::time_point{} + std::chrono::nanoseconds{index * 1'000'000'000LL / 60}, 30);
    EXPECT_EQ(accepted, 300);
}

TEST(FrameRatePolicy, SixtyTargetPreservesSixtyWithJitter) {
    FrameRateAdmission gate{};
    int accepted{};
    for (int index{}; index < 600; ++index) {
        const auto jitter = index % 2 == 0 ? 0ns : 1ms;
        accepted += gate.Admit(FrameRateAdmission::Clock::time_point{} + std::chrono::nanoseconds{index * 1'000'000'000LL / 60} + jitter, 60);
    }
    EXPECT_EQ(accepted, 600);
}

TEST(FrameRatePolicy, FastSourceDoesNotExceedEncoderBudget) {
    FrameRateAdmission gate{};
    int accepted{};
    for (int index{}; index < 1440; ++index)
        accepted += gate.Admit(FrameRateAdmission::Clock::time_point{} + std::chrono::nanoseconds{index * 1'000'000'000LL / 144}, 60);
    EXPECT_NEAR(accepted, 600, 1);
}

TEST(FrameRatePolicy, StallDoesNotCreateCatchupBurst) {
    FrameRateAdmission gate{};
    const auto begin = FrameRateAdmission::Clock::time_point{};
    EXPECT_TRUE(gate.Admit(begin, 60));
    EXPECT_TRUE(gate.Admit(begin + 1s, 60));
    EXPECT_FALSE(gate.Admit(begin + 1s, 60));
    EXPECT_FALSE(gate.Admit(begin + 1s + 1ms, 60));
}

TEST(FrameRatePolicy, ChangesAndRestartHaveNoOldDeadline) {
    FrameRateAdmission gate{};
    const auto begin = FrameRateAdmission::Clock::time_point{};
    for (int index{}; index < 20; ++index) {
        EXPECT_TRUE(gate.Admit(begin, 30));
        EXPECT_TRUE(gate.Admit(begin, 60));
    }
    gate = FrameRateAdmission{};
    EXPECT_TRUE(gate.Admit(begin, 60));
    EXPECT_FALSE(gate.Admit(begin, 0));
    EXPECT_FALSE(gate.Admit(begin, 121));
}
