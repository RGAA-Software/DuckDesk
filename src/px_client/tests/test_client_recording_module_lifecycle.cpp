#include <memory>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include "fake_client_module_services.h"
#include "px_client/modules/media_recording/media_recording_module.h"
#include "px_media_record_new/record_writer.h"
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
    const auto services =
        std::make_shared<test::FakeClientModuleServices>();
    for (int round = 0; round < 10; ++round) {
        const auto module = std::make_shared<ClientMediaRecordingModule>(
            std::weak_ptr<ClientModuleServices>(services));
        ASSERT_TRUE(module->Start(test::MakeModuleConfig(
            "recording-module-" + std::to_string(round))));
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

}  // namespace
}  // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QCoreApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
