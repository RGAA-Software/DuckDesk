#include <gtest/gtest.h>

#include <memory>

#include "processors/frame_resizer_processor.h"

namespace px::render {
namespace {

TEST(FrameResizerProcessorTest, RepeatedStartStopAndInvalidInputAreSafe) {
    auto processor = FrameResizerProcessor::Create();
    ASSERT_TRUE(processor);
    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(processor->Start());
        const Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        const Microsoft::WRL::ComPtr<ID3D11Device> device;
        const Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        EXPECT_FALSE(processor->Process(
            texture, device, context, 1, "monitor-a", 1280, 720));
        EXPECT_FALSE(processor->Snapshot("monitor-a").has_value());
        processor->ClearAdapter(1);
        ASSERT_TRUE(processor->Stop());
    }
    ASSERT_TRUE(processor->Stop());
}

TEST(FrameResizerProcessorTest, DisableIsIdempotent) {
    auto processor = FrameResizerProcessor::Create();
    ASSERT_TRUE(processor->Start());
    ASSERT_TRUE(processor->SetEnabled(false));
    ASSERT_TRUE(processor->SetEnabled(false));
    ASSERT_TRUE(processor->SetEnabled(true));
    ASSERT_TRUE(processor->Stop());
}

}  // namespace
}  // namespace px::render
