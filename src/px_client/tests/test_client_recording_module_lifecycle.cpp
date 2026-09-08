#include <memory>
#include <algorithm>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include "fake_client_module_services.h"
#include "px_client/modules/media_recording/media_recording_module.h"
#include "px_media_record/record_writer.h"
#include "px_message.pb.h"

namespace px {
namespace {

TEST(ClientRecordingModuleLifecycle, StaticRecordingCoreOwnsAndStopsWriter) {
    const auto writer = RecordWriter::Make(RecordWriterConfig{
        .monitor_name = "module-core-test",
    });
    ASSERT_TRUE(writer->IsRecording());
    writer->Stop();
    EXPECT_FALSE(writer->IsRecording());
    writer->Stop();
}

TEST(ClientRecordingModuleLifecycle, RepeatedStartQueueStopRejectsLateFrames) {
    const auto services = std::make_shared<test::FakeClientModuleServices>();
    for (int round = 0; round < 10; ++round) {
        const auto module = std::make_shared<ClientMediaRecordingModule>(std::weak_ptr<ClientModuleServices>(services));
        ASSERT_TRUE(module->Start(test::MakeModuleConfig("recording-module-" + std::to_string(round))));
        module->StartRecording();
        module->StopRecording();
        for (int index = 0; index < 64; ++index) {
            auto message = std::make_shared<Message>();
            message->set_type(MessageType::kAudioFrame);
            module->HandleMessage(message);
        }
        module->Stop();
        module->Stop();

        auto late_message = std::make_shared<Message>();
        late_message->set_type(MessageType::kAudioFrame);
        module->HandleMessage(late_message);
    }
}

TEST(ClientRecordingModuleLifecycle, FailedRunsKeepTheirOwnUiIntent) {
    const auto services = std::make_shared<test::FakeClientModuleServices>();
    const auto module = std::make_shared<ClientMediaRecordingModule>(std::weak_ptr<ClientModuleServices>(services));
    ASSERT_TRUE(module->Start(test::MakeModuleConfig("recording-intent-test")));
    auto invalid_video = std::make_shared<Message>();
    invalid_video->set_type(kVideoFrame);
    auto& video = *invalid_video->mutable_video_frame();
    video.set_type(static_cast<VideoType>(99));
    video.set_data("unknown-codec");
    video.set_frame_width(64);
    video.set_frame_height(64);
    module->StartRecording(3);
    module->HandleMessage(invalid_video);
    module->StartRecording(7); // A later intent also replaces a run whose queued Stop was superseded.
    module->HandleMessage(invalid_video);
    module->Stop();
    auto intents = services->RecordingFailureIntents();
    std::sort(intents.begin(), intents.end());
    EXPECT_EQ(intents, (std::vector<uint64_t>{3, 7}));
    EXPECT_EQ(services->recording_notifications_.load(), 0);
}

} // namespace
} // namespace px

int main(int argc, char** argv) { // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QCoreApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
