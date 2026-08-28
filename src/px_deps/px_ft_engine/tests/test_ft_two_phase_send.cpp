#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ft_engine.h"

namespace px::ft {
namespace {

class TwoPhaseTempDir final {
public:
    TwoPhaseTempDir() {
        path_ = std::filesystem::temp_directory_path() /
            ("ft_two_phase_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
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

void DeliverPrepared(const std::shared_ptr<FtEngine>& from,
                     const std::shared_ptr<FtEngine>& to,
                     int& delivered) {
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
    sender->SendFiles(temp.Path("source.bin").string(), false,
                      temp.Path("received.bin").string());

    int sender_delivered = 0;
    int receiver_delivered = 0;
    bool finished = false;
    for (int tick = 0; tick < 5000; ++tick) {
        sender->Tick();
        DeliverPrepared(sender, receiver, sender_delivered);
        receiver->Tick();
        DeliverPrepared(receiver, sender, receiver_delivered);
        if (sender->read_jobs().empty() && sender->write_jobs().empty() &&
            receiver->read_jobs().empty() && receiver->write_jobs().empty() &&
            !sender->HasPendingOutbound() && !receiver->HasPendingOutbound()) {
            finished = true;
            break;
        }
    }
    ASSERT_TRUE(finished);
    EXPECT_GT(sender_delivered, 2);
    EXPECT_GT(receiver_delivered, 0);

    std::ifstream input(temp.Path("received.bin"), std::ios::binary);
    const std::vector<char> received{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

} // namespace
} // namespace px::ft
