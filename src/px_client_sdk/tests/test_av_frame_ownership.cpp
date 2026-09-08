#include "av_frame_ref.h"
#include "platform/windows/windows_video_frame.h"
#include "ffmpeg_frame_adapter.h"
#include "ffmpeg_decoder_handles.h"
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
    auto image = MakeVulkanImage(*source);
    ASSERT_TRUE(image);
    image->full_color_ = true;
    auto copy = image->Clone();
    ASSERT_TRUE(copy);
    EXPECT_NE(VulkanFrameOf(image)->frame.get(), VulkanFrameOf(copy)->frame.get());
    source.reset();
    image.reset();
    EXPECT_EQ(copy->img_width, 8);
    EXPECT_EQ(copy->img_height, 8);
    EXPECT_EQ(copy->Size(), 0);
    EXPECT_EQ(copy->Format(), kRawImageVulkanAVFrame);
    EXPECT_TRUE(copy->full_color_);
    EXPECT_EQ(VulkanFrameOf(copy)->frame->pts, 73);
    EXPECT_EQ(VulkanFrameOf(copy)->frame->data[0][0], 42);
}

TEST(AvFrameOwnership, QueuedConsumerOwnsFrameAfterProducerDestruction) {
    std::vector<std::function<void()>> queue{};
    std::weak_ptr<const AVFrame> observed{};
    {
        const auto source = MakeOpaqueVulkanDescriptor();
        ASSERT_TRUE(source);
        const auto image = MakeVulkanImage(*source);
        ASSERT_TRUE(image);
        observed = VulkanFrameOf(image)->frame;
        queue.emplace_back([image] {
            EXPECT_EQ(VulkanFrameOf(image)->frame->pts, 73);
            EXPECT_EQ(VulkanFrameOf(image)->frame->data[0][0], 42);
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
    const auto image = MakeVulkanImage(*source);
    ASSERT_TRUE(image);
    ASSERT_EQ(av_frame_make_writable(source.get()), 0);
    source->data[0][0] = 99;
    source.reset();
    const auto copy = image->Clone();
    ASSERT_TRUE(copy);
    EXPECT_EQ(VulkanFrameOf(copy)->frame->format, AV_PIX_FMT_GRAY8);
    EXPECT_EQ(VulkanFrameOf(copy)->frame->data[0][0], 42);
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
            const auto image = MakeVulkanImage(*source);
            ASSERT_TRUE(image);
            observed = VulkanFrameOf(image)->frame;
            queue.emplace_back([image] { FAIL() << "Cancelled frame must not be consumed"; });
        }
        queue.clear();
        EXPECT_TRUE(observed.expired());
    }
}

TEST(AvFrameOwnership, InvalidVulkanCarrierDoesNotPublishAPartialFrame) {
    const auto empty = AllocateAvFrame();
    ASSERT_TRUE(empty);
    EXPECT_FALSE(MakeVulkanImage(*empty));
    empty->format = AV_PIX_FMT_VULKAN;
    EXPECT_FALSE(MakeVulkanImage(*empty));
    empty->format = AV_PIX_FMT_NB;
    empty->width = 8;
    empty->height = 8;
    EXPECT_FALSE(MakeVulkanImage(*empty));
}

TEST(AvFrameOwnership, CpuLayoutRejectsOverflowAndSupportsOddChromaSizes) {
    EXPECT_FALSE(RawImage::Make(kRawImageRGBA, INT_MAX, INT_MAX));
    EXPECT_FALSE(RawImage::Make(kRawImageI420, -1, 2));
    EXPECT_FALSE(RawImage::Make(kRawImageI420, 2, 0));
    EXPECT_FALSE(RawImage::Make(kRawImagePresented, 2, 2));
    const auto image = RawImage::Make(kRawImageI420, 3, 3);
    ASSERT_TRUE(image);
    EXPECT_EQ(image->Size(), 17);
    EXPECT_EQ(image->Plane(0).size(), 9);
    EXPECT_EQ(image->Plane(1).size(), 4);
    EXPECT_EQ(image->Plane(2).size(), 4);
    EXPECT_TRUE(image->Plane(3).empty());
    for (const auto byte : image->Bytes())
        EXPECT_EQ(byte, 0);
    EXPECT_FALSE(RawImage::Make(kRawImageRGBA, 2, 2, image->Bytes()));
}

TEST(AvFrameOwnership, CpuCopyRespectsSourcePaddingAndDoesNotRewriteQueuedFrame) {
    auto source = AllocateAvFrame();
    ASSERT_TRUE(source);
    source->format = AV_PIX_FMT_YUV420P;
    source->width = 3;
    source->height = 3;
    ASSERT_EQ(av_frame_get_buffer(source.get(), 32), 0);
    source->data[0][0] = 42;
    source->data[0][source->linesize[0] * 2 + 2] = 11;
    source->data[1][source->linesize[1] + 1] = 17;
    std::shared_ptr<RawImage> cache{};
    auto queued = CopyCpuAvFrame(*source, cache);
    ASSERT_TRUE(queued);
    EXPECT_EQ(queued->Plane(0)[8], 11);
    EXPECT_EQ(queued->Plane(1)[3], 17);
    source->data[0][0] = 99;
    const auto next = CopyCpuAvFrame(*source, cache);
    ASSERT_TRUE(next);
    EXPECT_NE(queued, next);
    EXPECT_EQ(queued->Plane(0)[0], 42);
    EXPECT_EQ(next->Plane(0)[0], 99);
    source.reset();
    cache.reset();
    EXPECT_EQ(queued->Plane(1)[3], 17);
}

TEST(AvFrameOwnership, CpuConversionHandlesNegativeStrideAndNv12Chroma) {
    auto source = AllocateAvFrame();
    ASSERT_TRUE(source);
    source->format = AV_PIX_FMT_NV12;
    source->width = 3;
    source->height = 3;
    ASSERT_EQ(av_frame_get_buffer(source.get(), 32), 0);
    source->data[0][2 * source->linesize[0]] = 31;
    source->data[0] += 2 * source->linesize[0];
    source->linesize[0] = -source->linesize[0];
    source->data[1][0] = 12;
    source->data[1][1] = 13;
    source->data[1][source->linesize[1] + 2] = 22;
    source->data[1][source->linesize[1] + 3] = 23;
    std::shared_ptr<RawImage> cache{};
    const auto image = CopyCpuAvFrame(*source, cache);
    ASSERT_TRUE(image);
    EXPECT_EQ(image->Plane(0)[0], 31);
    EXPECT_EQ(image->Plane(1)[0], 12);
    EXPECT_EQ(image->Plane(2)[0], 13);
    EXPECT_EQ(image->Plane(1)[3], 22);
    EXPECT_EQ(image->Plane(2)[3], 23);
    source->linesize[1] = 1;
    EXPECT_FALSE(CopyCpuAvFrame(*source, cache));
}

TEST(AvFrameOwnership, VulkanCloneRetainsExternalDeviceUntilLastFrameIsDestroyed) {
    const auto source = MakeOpaqueVulkanDescriptor();
    ASSERT_TRUE(source);
    auto device = AvBufferPtr(av_buffer_alloc(8), AvBufferDeleter{});
    ASSERT_TRUE(device);
    const std::weak_ptr<AVBufferRef> observed = device;
    auto image = MakeVulkanImage(*source, device);
    ASSERT_TRUE(image);
    auto copy = image->Clone();
    ASSERT_TRUE(copy);
    device.reset();
    image.reset();
    EXPECT_FALSE(observed.expired());
    copy.reset();
    EXPECT_TRUE(observed.expired());
}

TEST(AvFrameOwnership, DecoderPacketOwnsInputAndZeroPaddingAcrossReuse) {
    const DecoderPacketPtr packet{av_packet_alloc()};
    ASSERT_TRUE(packet);
    std::vector<std::uint8_t> input{1, 2, 3};
    ASSERT_TRUE(PrepareDecoderPacket(*packet, input));
    input[0] = 99;
    EXPECT_EQ(packet->data[0], 1);
    for (int index{}; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
        EXPECT_EQ(packet->data[packet->size + index], 0);
    EXPECT_FALSE(PrepareDecoderPacket(*packet, {}));
    EXPECT_EQ(packet->size, 0);
    ASSERT_TRUE(PrepareDecoderPacket(*packet, input));
    EXPECT_EQ(packet->data[0], 99);
}

TEST(AvFrameOwnership, PresentedFrameIsMetadataNotAnEmptyCpuImage) {
    const auto image = RawImage::MakePresented(1920, 1080);
    ASSERT_TRUE(image);
    EXPECT_EQ(image->Format(), kRawImagePresented);
    EXPECT_TRUE(image->Bytes().empty());
    EXPECT_FALSE(image->Platform());
    EXPECT_FALSE(RawImage::MakePresented(0, 1080));
}

} // namespace
} // namespace px
