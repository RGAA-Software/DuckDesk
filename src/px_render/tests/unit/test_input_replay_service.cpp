#include <gtest/gtest.h>

#include <memory>

#include "app/app_messages.h"
#include "px_capture/capture_message.h"
#include "px_message.pb.h"
#include "services/input_replay_service.h"

namespace px::render {
namespace {

TEST(InputReplayServiceTest, HundredStartStopRoundsReleaseState) {
    const auto service = InputReplayService::Create();
    ASSERT_TRUE(service);
    for (int round = 0; round < 100; ++round) {
        ASSERT_TRUE(service->Start());
        EXPECT_TRUE(service->Snapshot().running);
        service->UpdateCaptureMonitorInfo(CaptureMonitorInfoMessage{});
        const auto ignored = std::make_shared<Message>();
        ignored->set_type(MessageType::kHeartBeat);
        service->HandleMessage(ignored);
        ASSERT_TRUE(service->Stop());
        EXPECT_FALSE(service->Snapshot().running);
    }
    ASSERT_TRUE(service->Stop());
}

TEST(InputReplayServiceTest, DisableIsIdempotentAndRejectsMessages) {
    const auto service = InputReplayService::Create();
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(service->SetEnabled(false));
    ASSERT_TRUE(service->SetEnabled(false));
    const auto ignored = std::make_shared<Message>();
    ignored->set_type(MessageType::kHeartBeat);
    service->HandleMessage(ignored);
    EXPECT_EQ(service->Snapshot().accepted_messages, 0U);
    ASSERT_TRUE(service->SetEnabled(true));
    ASSERT_TRUE(service->Stop());
}

}  // namespace
}  // namespace px::render
