#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>

#include "px_common_new/data.h"
#include "px_message.pb.h"
#include "services/file_transfer_service.h"

namespace px::render {
namespace {

struct SendState final {
    std::mutex mutex;
    std::size_t calls{0};
    std::string transport_id;
    std::string stream_id;
    std::string connection_id;
    std::shared_ptr<Data> message;
};

FileTransferService::SendCallback MakeAcceptedSender(
    const std::shared_ptr<SendState>& state) {
    return [state](const std::string& transport_id,
                   const std::string& stream_id,
                   const std::shared_ptr<Data>& message,
                   const std::string& connection_id) {
        std::lock_guard lock(state->mutex);
        ++state->calls;
        state->transport_id = transport_id;
        state->stream_id = stream_id;
        state->connection_id = connection_id;
        state->message = message;
        return FileTransferSendResult::Accepted();
    };
}

std::shared_ptr<Message> MakeReadDirectory(const std::string& stream_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kFileAction);
    message->set_stream_id(stream_id);
    message->mutable_file_action()->mutable_read_dir()->set_path(".");
    return message;
}

std::shared_ptr<Message> MakeSendRequest(const std::string& stream_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kFileAction);
    message->set_stream_id(stream_id);
    auto& send = *message->mutable_file_action()->mutable_send();
    send.set_id(73);
    send.set_file_num(4);
    send.set_path("not-used-while-disabled");
    return message;
}

TEST(FileTransferServiceTest, RepeatedStartStopReleasesAllState) {
    const auto state = std::make_shared<SendState>();
    const auto service = FileTransferService::Create(
        {.device_id = "render-device"}, MakeAcceptedSender(state), {}, {});

    for (int round = 0; round < 25; ++round) {
        ASSERT_TRUE(service->Start());
        service->HandleInbound(FileTransferInbound{
            .message = MakeReadDirectory("stream-cycle"),
            .logical_session_id = "logical-cycle",
            .transport_id = "transport-cycle",
            .connection_id = "connection-cycle",
        });
        EXPECT_EQ(service->Snapshot().sessions, 1U);
        ASSERT_TRUE(service->Stop());
        const auto snapshot = service->Snapshot();
        EXPECT_FALSE(snapshot.running);
        EXPECT_EQ(snapshot.sessions, 0U);
        EXPECT_EQ(snapshot.audits, 0U);
    }
    ASSERT_TRUE(service->Stop());
}

TEST(FileTransferServiceTest, DisabledActionIsRejectedOnItsInboundRoute) {
    const auto state = std::make_shared<SendState>();
    const auto service = FileTransferService::Create(
        {.device_id = "render-device"}, MakeAcceptedSender(state), {}, {});
    ASSERT_TRUE(service->Start());
    ASSERT_TRUE(service->SetEnabled(false));

    service->HandleInbound(FileTransferInbound{
        .message = MakeSendRequest("stream-disabled"),
        .logical_session_id = "logical-disabled",
        .transport_id = "transport-disabled",
        .connection_id = "connection-disabled",
    });

    std::shared_ptr<Data> reply;
    {
        std::lock_guard lock(state->mutex);
        EXPECT_EQ(state->calls, 1U);
        EXPECT_EQ(state->transport_id, "transport-disabled");
        EXPECT_EQ(state->stream_id, "stream-disabled");
        EXPECT_EQ(state->connection_id, "connection-disabled");
        reply = state->message;
    }
    ASSERT_TRUE(reply);
    Message parsed;
    ASSERT_TRUE(parsed.ParseFromArray(reply->Bytes().data(), reply->Size()));
    EXPECT_EQ(parsed.type(), MessageType::kFileResponse);
    EXPECT_EQ(parsed.device_id(), "render-device");
    ASSERT_TRUE(parsed.file_response().has_error());
    EXPECT_EQ(parsed.file_response().error().id(), 73);
    EXPECT_EQ(parsed.file_response().error().file_num(), 4);
    EXPECT_EQ(parsed.file_response().error().error(),
              "No permission of file transfer");
    EXPECT_EQ(service->Snapshot().rejected_messages, 1U);
    ASSERT_TRUE(service->Stop());
}

TEST(FileTransferServiceTest, StaleDisconnectCannotRetireReplacementRoute) {
    const auto state = std::make_shared<SendState>();
    const auto service = FileTransferService::Create(
        {.device_id = "render-device"}, MakeAcceptedSender(state), {}, {});
    ASSERT_TRUE(service->Start());
    service->HandleInbound(FileTransferInbound{
        .message = MakeReadDirectory("stream-route"),
        .logical_session_id = "logical-route",
        .transport_id = "transport-route",
        .connection_id = "connection-new",
    });
    ASSERT_EQ(service->Snapshot().sessions, 1U);

    service->HandleRouteDisconnected(FileTransferRouteDisconnected{
        .logical_session_id = "logical-route",
        .stream_id = "stream-route",
        .transport_id = "transport-route",
        .connection_id = "connection-old",
    });
    EXPECT_EQ(service->Snapshot().sessions, 1U);

    service->HandleRouteDisconnected(FileTransferRouteDisconnected{
        .logical_session_id = "logical-route",
        .stream_id = "stream-route",
        .transport_id = "transport-route",
        .connection_id = "connection-new",
    });
    EXPECT_EQ(service->Snapshot().sessions, 0U);
    ASSERT_TRUE(service->Stop());
}

TEST(FileTransferServiceTest, DestructionDrainsQueuedWorkWithoutOwnerCapture) {
    const auto state = std::make_shared<SendState>();
    auto service = FileTransferService::Create(
        {.device_id = "render-device"}, MakeAcceptedSender(state), {}, {});
    const std::weak_ptr<FileTransferService> weak_service = service;
    ASSERT_TRUE(service->Start());
    for (int index = 0; index < 20; ++index) {
        service->HandleInbound(FileTransferInbound{
            .message = MakeReadDirectory("stream-queued"),
            .logical_session_id = "logical-queued",
            .transport_id = "transport-queued",
            .connection_id = "connection-queued",
        });
    }
    service.reset();
    EXPECT_TRUE(weak_service.expired());
}

}  // namespace
}  // namespace px::render
