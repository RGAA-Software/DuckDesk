#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "pipeline/captured_media_pipeline.h"
#include "pipeline/encoded_media_bus.h"

namespace px::render {
namespace {

std::shared_ptr<const CapturedVideoFrame> MakeVideoFrame(
    const std::uint64_t frame_index = 1) {
    auto frame = CapturedVideoFrame::Create(
        FrameIdentity{
            .stream_id = "source-stream",
            .monitor_id = "monitor-a",
            .frame_index = frame_index,
        },
        2,
        2,
        VideoPixelFormat::kBgra8,
        MakeImmutableByteBuffer(std::string(16, 'v')));
    return frame
        ? std::make_shared<const CapturedVideoFrame>(std::move(*frame))
        : std::shared_ptr<const CapturedVideoFrame>{};
}

std::shared_ptr<const CapturedAudioFrame> MakeAudioFrame() {
    return std::make_shared<const CapturedAudioFrame>(CapturedAudioFrame{
        .stream_id = "source-stream",
        .sample_rate_hz = 48000,
        .channels = 2,
        .bits_per_sample = 16,
        .payload = MakeImmutableByteBuffer(std::string(1920, 'a')),
    });
}

TEST(CapturedMediaPipelineTest,
     SourceProcessorsAndObserverUseOneTypedFlow) {
    const auto bus = EncodedMediaBus::Create();
    const auto delivered_video =
        std::make_shared<std::shared_ptr<const CapturedVideoFrame>>();
    const auto delivered_audio =
        std::make_shared<std::shared_ptr<const CapturedAudioFrame>>();
    const auto pipeline = CapturedMediaPipeline::Create(
        [bus, delivered_video](
            const std::shared_ptr<const CapturedVideoFrame>& frame) {
            *delivered_video = frame;
            bus->PublishCapturedVideo(frame);
            return MediaSubmitResult{};
        },
        [bus, delivered_audio](
            const std::shared_ptr<const CapturedAudioFrame>& frame) {
            *delivered_audio = frame;
            bus->PublishCapturedAudio(frame);
            return MediaSubmitResult{};
        });
    ASSERT_TRUE(pipeline);

    const auto order = std::make_shared<std::vector<std::string>>();
    const auto later = std::make_shared<CapturedMediaPipeline::VideoProcessor>(
        [order](const std::shared_ptr<const CapturedVideoFrame>& frame) {
            order->push_back("later");
            return CapturedVideoProcessResult(frame);
        });
    const auto earlier = std::make_shared<CapturedMediaPipeline::VideoProcessor>(
        [order](const std::shared_ptr<const CapturedVideoFrame>& frame) {
            order->push_back("earlier");
            return CapturedVideoProcessResult(frame);
        });
    auto later_registration =
        pipeline->RegisterVideoProcessor("later", 20, later);
    auto earlier_registration =
        pipeline->RegisterVideoProcessor("earlier", 10, earlier);
    ASSERT_TRUE(later_registration);
    ASSERT_TRUE(earlier_registration);
    EXPECT_TRUE(pipeline->HasVideoProcessors());
    EXPECT_FALSE(pipeline->HasAudioProcessors());

    const auto observed_index = std::make_shared<std::uint64_t>(0);
    const auto observer = std::make_shared<EncodedMediaBus::CapturedVideoCallback>(
        [observed_index](
            const std::shared_ptr<const CapturedVideoFrame>& frame) {
            *observed_index = frame ? frame->Identity().frame_index : 0;
        });
    const auto observer_registration = bus->SubscribeCapturedVideo(observer);
    const auto source = pipeline->CreateSourcePort();
    ASSERT_TRUE(source);
    ASSERT_TRUE(source->PublishVideo(MakeVideoFrame(77)));
    ASSERT_TRUE(source->PublishAudio(MakeAudioFrame()));

    EXPECT_EQ(*order, (std::vector<std::string>{"earlier", "later"}));
    ASSERT_TRUE(*delivered_video);
    ASSERT_TRUE(*delivered_audio);
    EXPECT_EQ((*delivered_video)->Identity().frame_index, 77U);
    EXPECT_EQ(*observed_index, 77U);
    EXPECT_TRUE(observer_registration->IsActive());
    const auto snapshot = pipeline->Snapshot();
    EXPECT_EQ(snapshot.video_processors, 2U);
    EXPECT_EQ(snapshot.video_received, 1U);
    EXPECT_EQ(snapshot.video_delivered, 1U);
    EXPECT_EQ(snapshot.audio_received, 1U);
    EXPECT_EQ(snapshot.audio_delivered, 1U);
}

TEST(CapturedMediaPipelineTest,
     UnregisterDuringDispatchSkipsQueuedProcessor) {
    const auto delivered = std::make_shared<int>(0);
    const auto pipeline = CapturedMediaPipeline::Create(
        [delivered](const std::shared_ptr<const CapturedVideoFrame>&) {
            ++*delivered;
            return MediaSubmitResult{};
        },
        [](const std::shared_ptr<const CapturedAudioFrame>&) {
            return MediaSubmitResult{};
        });
    const auto second_calls = std::make_shared<int>(0);
    const auto second = std::make_shared<CapturedMediaPipeline::VideoProcessor>(
        [second_calls](const std::shared_ptr<const CapturedVideoFrame>& frame) {
            ++*second_calls;
            return CapturedVideoProcessResult(frame);
        });
    auto second_registration =
        pipeline->RegisterVideoProcessor("second", 20, second);
    ASSERT_TRUE(second_registration);
    const auto second_token =
        std::make_shared<std::shared_ptr<ScopedSubscription>>(
            *second_registration);
    const auto first = std::make_shared<CapturedMediaPipeline::VideoProcessor>(
        [second_token](const std::shared_ptr<const CapturedVideoFrame>& frame) {
            (*second_token)->Reset();
            return CapturedVideoProcessResult(frame);
        });
    auto first_registration =
        pipeline->RegisterVideoProcessor("first", 10, first);
    ASSERT_TRUE(first_registration);

    ASSERT_TRUE(pipeline->SubmitVideo(MakeVideoFrame()));
    EXPECT_EQ(*second_calls, 0);
    EXPECT_EQ(*delivered, 1);
    EXPECT_EQ(pipeline->Snapshot().video_processors, 1U);
    EXPECT_TRUE(pipeline->HasVideoProcessors());
}

TEST(CapturedMediaPipelineTest,
     ProcessorFailureAndOwnerExpiryAreSafe) {
    const auto delivered = std::make_shared<int>(0);
    auto pipeline = CapturedMediaPipeline::Create(
        [delivered](const std::shared_ptr<const CapturedVideoFrame>&) {
            ++*delivered;
            return MediaSubmitResult{};
        },
        [](const std::shared_ptr<const CapturedAudioFrame>&) {
            return MediaSubmitResult{};
        });
    const auto failing = std::make_shared<CapturedMediaPipeline::VideoProcessor>(
        [](const std::shared_ptr<const CapturedVideoFrame>&) {
            return CapturedVideoProcessResult(std::unexpected(RenderError{
                .code = RenderErrorCode::kPipelineProcessorFailed,
                .component = "test_processor",
                .operation = "process",
                .stage = "video",
                .reason = "injected failure",
                .recoverable = true,
            }));
        });
    auto registration =
        pipeline->RegisterVideoProcessor("failing", 0, failing);
    ASSERT_TRUE(registration);
    const auto source = pipeline->CreateSourcePort();
    const auto failed = source->PublishVideo(MakeVideoFrame());
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, RenderErrorCode::kPipelineProcessorFailed);
    EXPECT_EQ(*delivered, 0);
    EXPECT_EQ(pipeline->Snapshot().video_failed, 1U);

    pipeline.reset();
    const auto expired = source->PublishVideo(MakeVideoFrame());
    ASSERT_FALSE(expired);
    EXPECT_EQ(expired.error().code,
              RenderErrorCode::kModuleDependencyUnavailable);
}

TEST(CapturedMediaPipelineTest,
     AudioProcessorCanDropAndExpiredCallbackIsRemoved) {
    const auto audio_deliveries = std::make_shared<int>(0);
    const auto pipeline = CapturedMediaPipeline::Create(
        [](const std::shared_ptr<const CapturedVideoFrame>&) {
            return MediaSubmitResult{};
        },
        [audio_deliveries](const std::shared_ptr<const CapturedAudioFrame>&) {
            ++*audio_deliveries;
            return MediaSubmitResult{};
        });
    auto dropper = std::make_shared<CapturedMediaPipeline::AudioProcessor>(
        [](const std::shared_ptr<const CapturedAudioFrame>&) {
            return CapturedAudioProcessResult(
                std::shared_ptr<const CapturedAudioFrame>{});
        });
    auto registration =
        pipeline->RegisterAudioProcessor("dropper", 0, dropper);
    ASSERT_TRUE(registration);
    EXPECT_TRUE(pipeline->HasAudioProcessors());
    ASSERT_TRUE(pipeline->SubmitAudio(MakeAudioFrame()));
    EXPECT_EQ(*audio_deliveries, 0);
    EXPECT_EQ(pipeline->Snapshot().audio_dropped, 1U);

    dropper.reset();
    EXPECT_FALSE(pipeline->HasAudioProcessors());
    ASSERT_TRUE(pipeline->SubmitAudio(MakeAudioFrame()));
    EXPECT_EQ(*audio_deliveries, 1);
    EXPECT_EQ(pipeline->Snapshot().audio_processors, 0U);
}

}  // namespace
}  // namespace px::render
