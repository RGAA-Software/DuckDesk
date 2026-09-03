#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "architecture/services/voice_call_service.h"
#include "px_message.pb.h"

namespace px::render {
namespace {

std::shared_ptr<Message> MakeCallRequest(const std::string& stream_id,
                                         const std::string& call_id,
                                         const std::uint64_t request_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kVoiceCallRequest);
    message->set_device_id("visitor");
    message->set_stream_id(stream_id);
    auto& request = *message->mutable_voice_call_request();
    request.set_connect(true);
    request.set_call_id(call_id);
    request.set_request_id(request_id);
    return message;
}

std::shared_ptr<VoiceCallService> MakeService(
    VoiceCallService::ConsentDelivery consent_delivery) {
    return VoiceCallService::Create(
        true,
        [](std::function<void()>&& task) {
            if (task) {
                task();
            }
        },
        std::move(consent_delivery),
        [](const TransportRoute&, const std::shared_ptr<Data>&) {
            return true;
        },
        [](const TransportRoute&, const std::string&, bool) {
            return true;
        },
        [](const TransportRoute&, const std::string&,
           const std::shared_ptr<const std::vector<std::int16_t>>&,
           int, int) {
            return true;
        });
}

TEST(VoiceCallServiceTest, RepeatedStartStopHasNoRetainedRoutes) {
    const auto service = MakeService(
        [](const VoiceCallConsentNotice&) { return true; });
    for (int round = 0; round < 10; ++round) {
        ASSERT_TRUE(service->Start());
        service->HandleClientConnected(
            "visitor", "stream", "RTC", "rtc-local");
        EXPECT_TRUE(service->Stop());
        const auto snapshot = service->Snapshot();
        EXPECT_FALSE(snapshot.running);
    }
}

TEST(VoiceCallServiceTest, TypedInboundProducesTypedConsentNotice) {
    auto notices = std::make_shared<std::vector<VoiceCallConsentNotice>>();
    const auto service = MakeService(
        [notices](const VoiceCallConsentNotice& notice) {
            notices->push_back(notice);
            return true;
        });
    ASSERT_TRUE(service->Start());
    service->HandleClientConnected("visitor", "stream", "WS", "net-ws");
    service->HandleMessage(MakeCallRequest("stream", "call", 7));

    ASSERT_EQ(notices->size(), 1U);
    EXPECT_TRUE(notices->front().show);
    EXPECT_EQ(notices->front().stream_id, "stream");
    EXPECT_EQ(notices->front().call_id, "call");
    EXPECT_EQ(notices->front().request_id, 7U);
    EXPECT_EQ(service->Snapshot().inbound_messages, 1U);
    EXPECT_TRUE(service->Stop());
}

TEST(VoiceCallServiceTest, ConsentCallbackMayStopServiceDuringDispatch) {
    std::weak_ptr<VoiceCallService> weak_service;
    const auto service = MakeService(
        [&weak_service](const VoiceCallConsentNotice&) {
            if (const auto owner = weak_service.lock()) {
                static_cast<void>(owner->Stop());
            }
            return true;
        });
    weak_service = service;
    ASSERT_TRUE(service->Start());
    service->HandleClientConnected("visitor", "stream", "WS", "net-ws");
    service->HandleMessage(MakeCallRequest("stream", "call", 9));
    EXPECT_FALSE(service->Snapshot().running);
}

TEST(VoiceCallServiceTest, MissingPanelIsRejectedWithoutDanglingCallback) {
    const auto service = MakeService(
        [](const VoiceCallConsentNotice&) { return false; });
    ASSERT_TRUE(service->Start());
    service->HandleClientConnected("visitor", "stream", "WS", "net-ws");
    service->HandleMessage(MakeCallRequest("stream", "call", 11));
    const auto snapshot = service->Snapshot();
    EXPECT_EQ(snapshot.consent_notices, 2U);
    EXPECT_GE(snapshot.rejected_outputs, 1U);
    EXPECT_TRUE(service->Stop());
}

}  // namespace
}  // namespace px::render
