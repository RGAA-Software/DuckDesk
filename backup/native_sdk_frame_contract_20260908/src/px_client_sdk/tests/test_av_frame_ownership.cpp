#include "av_frame_ref.h"
#include "gl/raw_image.h"

#include <gtest/gtest.h>
#include <functional>
#include <vector>

namespace px {
namespace {

AvFramePtr MakeCpuFrame() {
    auto frame = AllocateAvFrame();
    if (!frame)
        return {};
    frame->format = AV_PIX_FMT_GRAY8;
    frame->width = 8;
    frame->height = 8;
    frame->pts = 73;
    if (av_frame_get_buffer(frame.get(), 32) < 0)
        return {};
    frame->data[0][0] = 42;
    return frame;
}

// Synthetic opaque descriptor: exercise reference ownership without a GPU.
// It must never be passed to a Vulkan renderer or hardware decoder.
AvFramePtr MakeOpaqueVulkanDescriptor() {
    auto frame = MakeCpuFrame();
    if (frame)
        frame->format = AV_PIX_FMT_VULKAN;
    return frame;
}

TEST(AvFrameOwnership, SnapshotSurvivesSourceReuseAndDestruction) {
    auto source = MakeCpuFrame();
    ASSERT_TRUE(source);
    const auto snapshot = CloneAvFrame(*source);
    ASSERT_TRUE(snapshot);
    EXPECT_NE(source.get(), snapshot.get());
    ASSERT_EQ(av_frame_make_writable(source.get()), 0);
    source->data[0][0] = 99;
    source->pts = 74;
    av_frame_unref(source.get());
    source.reset();
    EXPECT_EQ(snapshot->pts, 73);
    EXPECT_EQ(snapshot->data[0][0], 42);
}

TEST(AvFrameOwnership, VulkanCarrierPreservesMetadataAndCloneOwnership) {
    auto source = MakeOpaqueVulkanDescriptor();
    ASSERT_TRUE(source);
    auto image = RawImage::MakeVulkanAVFrame(*source);
    ASSERT_TRUE(image);
    image->full_color_ = true;
    auto copy = image->Clone();
    ASSERT_TRUE(copy);
    EXPECT_NE(image->vulkan_av_frame_.get(), copy->vulkan_av_frame_.get());
    source.reset();
    image.reset();
    EXPECT_EQ(copy->img_width, 8);
    EXPECT_EQ(copy->img_height, 8);
    EXPECT_EQ(copy->Size(), 0);
    EXPECT_EQ(copy->Format(), kRawImageVulkanAVFrame);
    EXPECT_TRUE(copy->full_color_);
    EXPECT_EQ(copy->vulkan_av_frame_->pts, 73);
    EXPECT_EQ(copy->vulkan_av_frame_->data[0][0], 42);
}

TEST(AvFrameOwnership, QueuedConsumerOwnsFrameAfterProducerDestruction) {
    std::vector<std::function<void()>> queue{};
    std::weak_ptr<const AVFrame> observed{};
    {
        const auto source = MakeOpaqueVulkanDescriptor();
        ASSERT_TRUE(source);
        const auto image = RawImage::MakeVulkanAVFrame(*source);
        ASSERT_TRUE(image);
        observed = image->vulkan_av_frame_;
        queue.emplace_back([image] {
            EXPECT_EQ(image->vulkan_av_frame_->pts, 73);
            EXPECT_EQ(image->vulkan_av_frame_->data[0][0], 42);
        });
    }
    ASSERT_FALSE(observed.expired());
    queue.front()();
    queue.clear();
    EXPECT_TRUE(observed.expired());
}

TEST(AvFrameOwnership, VulkanRendererAcceptsAndOwnsSoftwareDecodedFrames) {
    auto source = MakeCpuFrame();
    ASSERT_TRUE(source);
    const auto image = RawImage::MakeVulkanAVFrame(*source);
    ASSERT_TRUE(image);
    ASSERT_EQ(av_frame_make_writable(source.get()), 0);
    source->data[0][0] = 99;
    source.reset();
    const auto copy = image->Clone();
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy->vulkan_av_frame_->format, AV_PIX_FMT_GRAY8);
    EXPECT_EQ(copy->vulkan_av_frame_->data[0][0], 42);
    EXPECT_EQ(copy->img_width, 8);
    EXPECT_EQ(copy->img_height, 8);
}

TEST(AvFrameOwnership, CancellingQueuedWorkReleasesFramesAcrossRepeatedLifecycles) {
    for (int iteration{0}; iteration < 64; ++iteration) {
        std::vector<std::function<void()>> queue{};
        std::weak_ptr<const AVFrame> observed{};
        {
            const auto source = MakeOpaqueVulkanDescriptor();
            ASSERT_TRUE(source);
            const auto image = RawImage::MakeVulkanAVFrame(*source);
            ASSERT_TRUE(image);
            observed = image->vulkan_av_frame_;
            queue.emplace_back([image] { FAIL() << "Cancelled frame must not be consumed"; });
        }
        queue.clear();
        EXPECT_TRUE(observed.expired());
    }
}

TEST(AvFrameOwnership, InvalidVulkanCarrierDoesNotPublishAPartialFrame) {
    const auto empty = AllocateAvFrame();
    ASSERT_TRUE(empty);
    EXPECT_FALSE(RawImage::MakeVulkanAVFrame(*empty));
    empty->format = AV_PIX_FMT_VULKAN;
    EXPECT_FALSE(RawImage::MakeVulkanAVFrame(*empty));
    empty->format = AV_PIX_FMT_NB;
    empty->width = 8;
    empty->height = 8;
    EXPECT_FALSE(RawImage::MakeVulkanAVFrame(*empty));
}

} // namespace
} // namespace px
