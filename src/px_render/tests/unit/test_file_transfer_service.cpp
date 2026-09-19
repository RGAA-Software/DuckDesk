#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "px_common/data.h"
#include "px_common/uuid.h"
#include "px_ft_engine/ft_path.h"
#include "px_ft_engine/ft_sha256.h"
#include "px_ft_engine/transfer_job.h"
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

struct AuditState final {
    std::mutex mutex{};
    std::condition_variable changed{};
    std::optional<FileTransferAuditBegin> begin{};
    std::optional<FileTransferAuditEnd> end{};
};

class TestDirectory final {
public:
    TestDirectory() : path_(std::filesystem::temp_directory_path() / ("pixels-ft-service-" + GetUUID())) {
        std::filesystem::create_directories(path_);
    }

    ~TestDirectory() {
        std::error_code ignored{};
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_{};
};

std::string Sha256(std::string_view content) {
    std::vector<std::uint8_t> bytes{};
    bytes.reserve(content.size());
    for (const auto byte : content) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    auto hasher = ft::Sha256Hasher{};
    hasher.Update(bytes);
    return ft::Sha256Bytes(hasher.Finalize());
}

std::shared_ptr<Message> MakeFileResponse(const std::string& stream_id) {
    auto message = std::make_shared<Message>();
    message->set_type(MessageType::kFileResponse);
    message->set_stream_id(stream_id);
    return message;
}

FileTransferService::SendCallback MakeAcceptedSender(const std::shared_ptr<SendState>& state) {
    return [state](const std::string& transport_id, const std::string& stream_id, const std::shared_ptr<Data>& message,
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
    const auto service = FileTransferService::Create({.device_id = "render-device"}, MakeAcceptedSender(state), {}, {}, {});

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
    const auto service = FileTransferService::Create({.device_id = "render-device"}, MakeAcceptedSender(state), {}, {}, {});
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
    EXPECT_EQ(parsed.file_response().error().error(), "No permission of file transfer");
    EXPECT_EQ(service->Snapshot().rejected_messages, 1U);
    ASSERT_TRUE(service->Stop());
}

TEST(FileTransferServiceTest, StaleDisconnectCannotRetireReplacementRoute) {
    const auto state = std::make_shared<SendState>();
    const auto service = FileTransferService::Create({.device_id = "render-device"}, MakeAcceptedSender(state), {}, {}, {});
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
    auto service = FileTransferService::Create({.device_id = "render-device"}, MakeAcceptedSender(state), {}, {}, {});
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

TEST(FileTransferServiceTest, SuccessfulUploadReportsVerifiedContentDigest) {
    constexpr std::string_view content = "verified file transfer payload";
    constexpr std::int32_t job_id = 91;
    const auto send_state = std::make_shared<SendState>();
    const auto audit_state = std::make_shared<AuditState>();
    const auto service = FileTransferService::Create(
        {.device_id = "render-device"}, MakeAcceptedSender(send_state),
        [audit_state](const FileTransferAuditBegin& audit) {
            {
                std::lock_guard lock(audit_state->mutex);
                audit_state->begin = audit;
            }
            audit_state->changed.notify_all();
        },
        {},
        [audit_state](const FileTransferAuditEnd& audit) {
            {
                std::lock_guard lock(audit_state->mutex);
                audit_state->end = audit;
            }
            audit_state->changed.notify_all();
        });
    ASSERT_TRUE(service->Start());

    const auto directory = std::make_shared<TestDirectory>();
    auto begin_message = std::make_shared<Message>();
    begin_message->set_type(MessageType::kFileAction);
    begin_message->set_stream_id("stream-upload");
    begin_message->set_device_id("visitor-device");
    auto& receive = *begin_message->mutable_file_action()->mutable_receive();
    receive.set_id(job_id);
    receive.set_path(ft::ToUtf8(directory->Path()));
    receive.set_file_num(0);
    receive.set_total_size(content.size());
    auto& file = *receive.add_files();
    file.set_entry_type(FileType::RegularFile);
    file.set_name("payload.bin");
    file.set_size(content.size());
    file.set_modified_time(1);
    const auto inbound = [&service](const std::shared_ptr<Message>& message) {
        service->HandleInbound(FileTransferInbound{
            .message = message,
            .logical_session_id = "6d593020-9ca6-4f84-84a3-c3478af87a3f",
            .transport_id = "transport-upload",
            .connection_id = "connection-upload",
        });
    };
    inbound(begin_message);

    auto digest_message = MakeFileResponse("stream-upload");
    auto& digest = *digest_message->mutable_file_response()->mutable_digest();
    digest.set_id(job_id);
    digest.set_file_num(0);
    digest.set_file_size(content.size());
    digest.set_last_modified(1);
    digest.set_capabilities(ft::kFtCurrentCapabilities);
    inbound(digest_message);

    auto block_message = MakeFileResponse("stream-upload");
    auto& block = *block_message->mutable_file_response()->mutable_block();
    block.set_id(job_id);
    block.set_file_num(0);
    block.set_blk_id(1);
    block.set_data(content);
    inbound(block_message);

    auto eof_message = MakeFileResponse("stream-upload");
    auto& eof = *eof_message->mutable_file_response()->mutable_block();
    eof.set_id(job_id);
    eof.set_file_num(0);
    eof.set_blk_id(2);
    eof.set_file_hash(Sha256(content));
    inbound(eof_message);

    auto done_message = MakeFileResponse("stream-upload");
    auto& done = *done_message->mutable_file_response()->mutable_done();
    done.set_id(job_id);
    done.set_file_num(0);
    inbound(done_message);

    std::unique_lock audit_lock(audit_state->mutex);
    ASSERT_TRUE(audit_state->changed.wait_for(audit_lock, std::chrono::seconds(5), [audit_state] { return audit_state->end.has_value(); }));
    ASSERT_TRUE(audit_state->begin.has_value());
    ASSERT_TRUE(audit_state->end.has_value());
    EXPECT_EQ(audit_state->begin->logical_session_id, "6d593020-9ca6-4f84-84a3-c3478af87a3f");
    EXPECT_EQ(audit_state->begin->file_name, "payload.bin");
    EXPECT_EQ(audit_state->begin->total_bytes, content.size());
    EXPECT_EQ(audit_state->begin->console_direction, FileTransferAuditDirection::kToNode);
    EXPECT_EQ(audit_state->end->transfer_request_id, audit_state->begin->transfer_request_id);
    EXPECT_TRUE(audit_state->end->success);
    EXPECT_EQ(audit_state->end->transferred_bytes, content.size());
    EXPECT_EQ(audit_state->end->console_outcome, FileTransferAuditOutcome::kCompleted);
    ASSERT_TRUE(audit_state->end->verified_sha256.has_value());
    EXPECT_EQ(std::string(audit_state->end->verified_sha256->begin(), audit_state->end->verified_sha256->end()), Sha256(content));
    audit_lock.unlock();

    EXPECT_TRUE(std::filesystem::exists(directory->Path() / "payload.bin"));
    ASSERT_TRUE(service->Stop());
}

}  // namespace
}  // namespace px::render
