#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "ft_engine.h"
#include "ft_async_session.h"

namespace px::ft {
namespace {

class TwoPhaseTempDir final {
  public:
    TwoPhaseTempDir() {
        path_ =
            std::filesystem::temp_directory_path() / ("ft_two_phase_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }

    ~TwoPhaseTempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& name) const {
        return path_ / name;
    }

  private:
    std::filesystem::path path_;
};

void DeliverPrepared(const std::shared_ptr<FtEngine>& from, const std::shared_ptr<FtEngine>& to, int& delivered) {
    while (const auto prepared = from->PrepareOutbound()) {
        ASSERT_TRUE(prepared->message);
        if (prepared->message->has_file_action()) {
            to->HandleFileAction(prepared->message->file_action());
        } else if (prepared->message->has_file_response()) {
            to->HandleFileResponse(prepared->message->file_response());
        }
        ASSERT_TRUE(from->CommitOutbound(prepared->token));
        ++delivered;
    }
}

TEST(FtTwoPhaseSend, PrepareRetryCommitKeepsOneStableMessage) {
    const auto engine = std::make_shared<FtEngine>();
    engine->ReceiveFiles("remote.bin", false, "local.bin");

    ASSERT_EQ(engine->PendingOutboundCount(), 1U);
    const auto first = engine->PrepareOutbound();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->message);
    EXPECT_TRUE(first->message->has_file_action());
    EXPECT_TRUE(first->message->file_action().has_send());

    EXPECT_TRUE(engine->RetryOutbound(first->token));
    const auto retry = engine->PrepareOutbound();
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(retry->token, first->token);
    EXPECT_EQ(retry->message, first->message);
    EXPECT_EQ(engine->PendingOutboundCount(), 1U);

    EXPECT_TRUE(engine->CommitOutbound(first->token));
    EXPECT_FALSE(engine->PrepareOutbound().has_value());
    EXPECT_EQ(engine->PendingOutboundCount(), 0U);
    EXPECT_FALSE(engine->CommitOutbound(first->token));
}

TEST(FtTwoPhaseSend, TickDoesNotReadAheadWhileOutboundIsPending) {
    const auto engine = std::make_shared<FtEngine>();
    engine->ReceiveFiles("remote.bin", false, "local.bin");
    ASSERT_EQ(engine->PendingOutboundCount(), 1U);

    for (int tick = 0; tick < 100; ++tick) {
        engine->Tick();
    }

    EXPECT_EQ(engine->PendingOutboundCount(), 1U);
}

TEST(FtTwoPhaseSend, FullTransferCommitsEveryMessageExactlyOnce) {
    TwoPhaseTempDir temp;
    std::vector<char> content(700 * 1024);
    for (std::size_t index = 0; index < content.size(); ++index) {
        content[index] = static_cast<char>((index * 31U) % 251U);
    }
    {
        std::ofstream output(temp.Path("source.bin"), std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    const auto sender = std::make_shared<FtEngine>();
    const auto receiver = std::make_shared<FtEngine>();
    sender->SendFiles(temp.Path("source.bin").string(), false, temp.Path("received.bin").string());

    int sender_delivered = 0;
    int receiver_delivered = 0;
    bool finished = false;
    for (int tick = 0; tick < 5000; ++tick) {
        sender->Tick();
        DeliverPrepared(sender, receiver, sender_delivered);
        receiver->Tick();
        DeliverPrepared(receiver, sender, receiver_delivered);
        if (sender->read_jobs().empty() && sender->write_jobs().empty() && receiver->read_jobs().empty() && receiver->write_jobs().empty() &&
            !sender->HasPendingOutbound() && !receiver->HasPendingOutbound()) {
            finished = true;
            break;
        }
    }
    ASSERT_TRUE(finished);
    EXPECT_GT(sender_delivered, 2);
    EXPECT_GT(receiver_delivered, 0);

    std::ifstream input(temp.Path("received.bin"), std::ios::binary);
    const std::vector<char> received{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(received, content);
}

TEST(FtTwoPhaseSend, RepeatedBusyDoesNotChangeTokenOrDuplicateQueueEntry) {
    const auto engine = std::make_shared<FtEngine>();
    engine->ReceiveFiles("remote.bin", false, "local.bin");
    const auto first = engine->PrepareOutbound();
    ASSERT_TRUE(first.has_value());

    for (int retry = 0; retry < 100; ++retry) {
        ASSERT_TRUE(engine->RetryOutbound(first->token));
        const auto current = engine->PrepareOutbound();
        ASSERT_TRUE(current.has_value());
        EXPECT_EQ(current->token, first->token);
        EXPECT_EQ(current->message, first->message);
        EXPECT_EQ(engine->PendingOutboundCount(), 1U);
    }

    EXPECT_TRUE(engine->CommitOutbound(first->token));
    EXPECT_EQ(engine->PendingOutboundCount(), 0U);
}

TEST(FtTwoPhaseSend, UploadSuccessWaitsForReceiverFinalizationAcknowledgement) {
    TwoPhaseTempDir temp{};
    {
        std::ofstream output(temp.Path("source.bin"), std::ios::binary);
        output << "verified payload";
    }
    const auto sender = std::make_shared<FtEngine>();
    const auto receiver = std::make_shared<FtEngine>();
    const auto completed = std::make_shared<int>(0);
    sender->SetJobDoneCallback([completed](int32_t, int32_t, const std::string& error) {
        EXPECT_TRUE(error.empty());
        ++*completed;
    });
    sender->SendFiles(temp.Path("source.bin").string(), false, temp.Path("received.bin").string());
    int delivered = 0;
    bool awaiting_ack = false;
    for (int tick = 0; tick < 100; ++tick) {
        sender->Tick();
        DeliverPrepared(sender, receiver, delivered);
        const auto response = receiver->PrepareOutbound();
        if (response && response->message->has_file_response() && response->message->file_response().has_done()) {
            awaiting_ack = true;
            break;
        }
        DeliverPrepared(receiver, sender, delivered);
    }
    ASSERT_TRUE(awaiting_ack);
    EXPECT_TRUE(std::filesystem::exists(temp.Path("received.bin")));
    EXPECT_EQ(*completed, 0);
    EXPECT_EQ(sender->read_jobs().size(), 1U);
    for (int tick = 0; tick < 10; ++tick)
        sender->Tick();
    EXPECT_FALSE(sender->HasPendingOutbound()); // EOF is not resent while waiting.
    DeliverPrepared(receiver, sender, delivered);
    EXPECT_EQ(*completed, 1);
    EXPECT_TRUE(sender->read_jobs().empty());
    EXPECT_FALSE(sender->HasPendingOutbound()); // An ACK never generates an ACK loop.
}

TEST(FtTwoPhaseSend, AsyncSessionsTransferEightMiBWithVerifiedReceiverCompletion) {
    TwoPhaseTempDir temp{};
    const std::string content(8 * 1024 * 1024, 'x');
    {
        std::ofstream output(temp.Path("source.bin"), std::ios::binary);
        output << content;
    }
    const auto to_sender = std::make_shared<std::weak_ptr<FtAsyncSession>>();
    const auto to_receiver = std::make_shared<std::weak_ptr<FtAsyncSession>>();
    const auto route = [](const std::shared_ptr<std::weak_ptr<FtAsyncSession>>& peer) {
        return [peer](const std::shared_ptr<const px::Message>& message) {
            const auto session = peer->lock();
            if (!session || !session->Post("inbound", [message](const auto& engine) {
                    if (message->has_file_action())
                        engine->HandleFileAction(message->file_action());
                    if (message->has_file_response())
                        engine->HandleFileResponse(message->file_response());
                }))
                return FileTransferSendResult::Disconnected("peer stopped");
            return FileTransferSendResult::Accepted();
        };
    };
    const auto done = std::make_shared<std::promise<std::string>>();
    auto done_future = done->get_future();
    const auto sender = FtAsyncSession::Create(route(to_receiver), [done](const auto& engine) {
        engine->SetJobDoneCallback([done](int32_t, int32_t, const std::string& error) { done->set_value(error); });
    });
    const auto receiver = FtAsyncSession::Create(route(to_sender));
    *to_sender = sender;
    *to_receiver = receiver;
    ASSERT_TRUE(sender->Start());
    ASSERT_TRUE(receiver->Start());
    ASSERT_TRUE(sender->Post("upload", [source = temp.Path("source.bin").string(), target = temp.Path("received.bin").string()](const auto& engine) {
        engine->SendFiles(source, false, target);
    }));
    ASSERT_EQ(done_future.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_TRUE(done_future.get().empty());
    EXPECT_TRUE(sender->StopAndWait());
    EXPECT_TRUE(receiver->StopAndWait());
    std::ifstream input(temp.Path("received.bin"), std::ios::binary);
    const std::string received{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(received, content);
    EXPECT_FALSE(std::filesystem::exists(temp.Path("received.bin.download")));
}

TEST(FtTwoPhaseSend, ReceiverWriteErrorTerminatesTheSenderWithoutSuccess) {
    TwoPhaseTempDir temp{};
    {
        std::ofstream output(temp.Path("source.bin"), std::ios::binary);
        output << "payload";
    }
    const auto sender = std::make_shared<FtEngine>();
    const auto errors = std::make_shared<std::vector<std::string>>();
    sender->SetJobDoneCallback([errors](int32_t, int32_t, const std::string& error) { errors->push_back(error); });
    const auto id = sender->SendFiles(temp.Path("source.bin").string(), false, temp.Path("received.bin").string());
    sender->HandleFileResponse(NewError(id, "receiver disk full", 0).file_response());
    EXPECT_TRUE(sender->read_jobs().empty());
    ASSERT_EQ(errors->size(), 1U);
    EXPECT_EQ(errors->front(), "receiver disk full");
    sender->HandleFileResponse(NewDone(id, 0).file_response());
    EXPECT_EQ(errors->size(), 1U);
}

class FaultTransfer final {
  public:
    FaultTransfer() {
        std::ofstream output(temp.Path("source.bin"), std::ios::binary);
        output << content;
        sender->SetJobDoneCallback([events = sender_events](int32_t, int32_t, const std::string& error) { events->push_back(error); });
        receiver->SetJobDoneCallback([events = receiver_events](int32_t, int32_t, const std::string& error) { events->push_back(error); });
    }

    int32_t StartUpload() {
        return sender->SendFiles(temp.Path("source.bin").string(), false, temp.Path("received.bin").string());
    }

    void Pump() {
        int delivered = 0;
        sender->Tick();
        DeliverPrepared(sender, receiver, delivered);
        receiver->Tick();
        DeliverPrepared(receiver, sender, delivered);
    }

    bool ReachPartialWrite() {
        for (int tick = 0; tick < 100; ++tick) {
            Pump();
            if (!receiver->write_jobs().empty() && receiver->write_jobs().front().transferred() > 0)
                return true;
        }
        return false;
    }

    bool Finish() {
        for (int tick = 0; tick < 1000; ++tick) {
            Pump();
            if (sender->read_jobs().empty() && receiver->write_jobs().empty() && !sender->HasPendingOutbound() && !receiver->HasPendingOutbound())
                return true;
        }
        return false;
    }

    std::string Received() const {
        std::ifstream input(temp.Path("received.bin"), std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    TwoPhaseTempDir temp{};
    const std::string content = std::string(2 * 1024 * 1024, 'f');
    const std::shared_ptr<FtEngine> sender = std::make_shared<FtEngine>();
    const std::shared_ptr<FtEngine> receiver = std::make_shared<FtEngine>();
    const std::shared_ptr<std::vector<std::string>> sender_events = std::make_shared<std::vector<std::string>>();
    const std::shared_ptr<std::vector<std::string>> receiver_events = std::make_shared<std::vector<std::string>>();
};

TEST(FtTwoPhaseSend, CancelPartialUploadThenRetryProducesOnlyOneSuccess) {
    FaultTransfer transfer{};
    const auto cancelled = transfer.StartUpload();
    ASSERT_TRUE(transfer.ReachPartialWrite());
    transfer.sender->CancelJob(cancelled);
    ASSERT_TRUE(transfer.Finish());
    EXPECT_EQ(*transfer.sender_events, std::vector<std::string>{"cancel"});
    EXPECT_EQ(*transfer.receiver_events, std::vector<std::string>{"cancel"});
    EXPECT_FALSE(std::filesystem::exists(transfer.temp.Path("received.bin.download")));
    EXPECT_FALSE(std::filesystem::exists(transfer.temp.Path("received.bin.digest")));
    EXPECT_NE(transfer.StartUpload(), cancelled);
    ASSERT_TRUE(transfer.Finish());
    EXPECT_EQ(transfer.Received().size(), transfer.content.size());
    EXPECT_TRUE(transfer.Received() == transfer.content);
    EXPECT_EQ(*transfer.sender_events, (std::vector<std::string>{"cancel", ""}));
    EXPECT_EQ(*transfer.receiver_events, (std::vector<std::string>{"cancel", ""}));
}

TEST(FtTwoPhaseSend, DisconnectPartialUploadPreservesPartialFileAndRetryCompletes) {
    FaultTransfer transfer{};
    const auto interrupted = transfer.StartUpload();
    ASSERT_TRUE(transfer.ReachPartialWrite());
    transfer.sender->DisconnectCleanup();
    transfer.receiver->DisconnectCleanup();
    EXPECT_TRUE(transfer.sender_events->empty());
    EXPECT_TRUE(transfer.receiver_events->empty());
    EXPECT_TRUE(transfer.sender->read_jobs().empty());
    EXPECT_TRUE(transfer.receiver->write_jobs().empty());
    EXPECT_TRUE(std::filesystem::exists(transfer.temp.Path("received.bin.download")));
    EXPECT_TRUE(std::filesystem::exists(transfer.temp.Path("received.bin.digest")));
    EXPECT_NE(transfer.StartUpload(), interrupted);
    ASSERT_TRUE(transfer.Finish());
    EXPECT_EQ(transfer.Received().size(), transfer.content.size());
    EXPECT_TRUE(transfer.Received() == transfer.content);
    EXPECT_EQ(*transfer.sender_events, std::vector<std::string>{""});
    EXPECT_EQ(*transfer.receiver_events, std::vector<std::string>{""});
    EXPECT_FALSE(std::filesystem::exists(transfer.temp.Path("received.bin.download")));
}

TEST(FtTwoPhaseSend, ActualReceiverOpenFailureIsReportedToBothPeers) {
    FaultTransfer transfer{};
    // A directory cannot be opened as the receiver's regular output file.
    ASSERT_TRUE(std::filesystem::create_directory(transfer.temp.Path("received.bin.download")));
    transfer.StartUpload();
    ASSERT_TRUE(transfer.Finish());
    ASSERT_EQ(transfer.sender_events->size(), 1U);
    ASSERT_EQ(transfer.receiver_events->size(), 1U);
    EXPECT_FALSE(transfer.sender_events->front().empty());
    EXPECT_EQ(transfer.sender_events->front(), transfer.receiver_events->front());
    EXPECT_FALSE(std::filesystem::exists(transfer.temp.Path("received.bin")));
}

TEST(FtTwoPhaseSend, ActualReceiverRenameFailureNeverAcknowledgesSuccess) {
    FaultTransfer transfer{};
    transfer.StartUpload();
    bool blocked_finalize = false;
    int delivered = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        transfer.sender->Tick();
        const auto next = transfer.sender->PrepareOutbound();
        if (next && next->message->has_file_response() && next->message->file_response().has_done()) {
            ASSERT_TRUE(std::filesystem::create_directory(transfer.temp.Path("received.bin")));
            blocked_finalize = true;
        }
        DeliverPrepared(transfer.sender, transfer.receiver, delivered);
        DeliverPrepared(transfer.receiver, transfer.sender, delivered);
        if (blocked_finalize)
            break;
    }
    ASSERT_TRUE(blocked_finalize);
    ASSERT_EQ(transfer.sender_events->size(), 1U);
    ASSERT_EQ(transfer.receiver_events->size(), 1U);
    EXPECT_FALSE(transfer.sender_events->front().empty());
    EXPECT_EQ(transfer.sender_events->front(), transfer.receiver_events->front());
    EXPECT_TRUE(transfer.sender->read_jobs().empty());
    EXPECT_TRUE(transfer.receiver->write_jobs().empty());
    EXPECT_TRUE(std::filesystem::exists(transfer.temp.Path("received.bin.download")));
    EXPECT_TRUE(std::filesystem::exists(transfer.temp.Path("received.bin.digest")));
}

TEST(FtTwoPhaseSend, CancelWhileAwaitingAcknowledgementIgnoresLateDone) {
    FaultTransfer transfer{};
    const auto id = transfer.StartUpload();
    bool awaiting_ack = false;
    int delivered = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        transfer.sender->Tick();
        DeliverPrepared(transfer.sender, transfer.receiver, delivered);
        const auto next = transfer.receiver->PrepareOutbound();
        if (next && next->message->has_file_response() && next->message->file_response().has_done()) {
            awaiting_ack = true;
            break;
        }
        DeliverPrepared(transfer.receiver, transfer.sender, delivered);
    }
    ASSERT_TRUE(awaiting_ack);
    ASSERT_TRUE(transfer.sender_events->empty());
    transfer.sender->CancelJob(id);
    DeliverPrepared(transfer.receiver, transfer.sender, delivered);
    EXPECT_EQ(*transfer.sender_events, std::vector<std::string>{"cancel"});
    EXPECT_TRUE(transfer.sender->read_jobs().empty());
}

} // namespace
} // namespace px::ft
