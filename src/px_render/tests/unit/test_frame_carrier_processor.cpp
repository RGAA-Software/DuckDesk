#include <gtest/gtest.h>

#include <memory>

#include "processors/frame_carrier_processor.h"

namespace px::render {
namespace {

TEST(FrameCarrierProcessorTest, RepeatedStartStopAndInvalidInputAreSafe) {
    const auto processor = FrameCarrierProcessor::Create({});
    ASSERT_TRUE(processor);
    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(processor->Start()) << "round " << round;
        EXPECT_FALSE(processor->InitializeMonitor(FrameCarrierParams{
            .monitor_id = "monitor-a",
            .adapter_uid = 7,
        }));
        EXPECT_FALSE(processor->CopyTexture("monitor-a", 1, 1));
        processor->ClearAdapter(7);
        ASSERT_TRUE(processor->Stop()) << "round " << round;
        EXPECT_FALSE(processor->Snapshot().running);
        EXPECT_EQ(processor->Snapshot().active_monitors, 0U);
    }
    ASSERT_TRUE(processor->Stop());
}

TEST(FrameCarrierProcessorTest, DisableAndEnableAreIdempotent) {
    const auto processor = FrameCarrierProcessor::Create({});
    ASSERT_TRUE(processor->Start());
    ASSERT_TRUE(processor->SetEnabled(false));
    ASSERT_TRUE(processor->SetEnabled(false));
    EXPECT_FALSE(processor->Snapshot().enabled);
    ASSERT_TRUE(processor->SetEnabled(true));
    ASSERT_TRUE(processor->SetEnabled(true));
    EXPECT_TRUE(processor->Snapshot().enabled);
    ASSERT_TRUE(processor->Stop());
}

}  // namespace
}  // namespace px::render
