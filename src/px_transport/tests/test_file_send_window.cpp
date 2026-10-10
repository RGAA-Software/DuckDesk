#include <gtest/gtest.h>

#include <future>

#include "file_send_window.h"
#include "message_session.h"
#include "px_common/data.h"
#include "px_message.pb.h"

namespace px::transport {
namespace {
using namespace std::chrono_literals;

TEST(FileSendWindow, CreditRequiresMonotonicPeerReceiptAndBoundsOversizedMessages) {
    FileSendWindow window{};
    ASSERT_TRUE(window.Reserve(120 * 1024));
    ASSERT_TRUE(window.Reserve(120 * 1024));
    EXPECT_FALSE(window.Reserve(120 * 1024));
    EXPECT_FALSE(window.Acknowledge(241 * 1024));
    EXPECT_EQ(window.Acknowledge(120 * 1024), std::optional<bool>{true});
    EXPECT_EQ(window.Acknowledge(120 * 1024), std::optional<bool>{false});
    EXPECT_FALSE(window.Acknowledge(1));
    ASSERT_TRUE(window.Reserve(120 * 1024));
    EXPECT_FALSE(window.Reserve(FileSendWindow::kMaximumMessage));
    ASSERT_EQ(window.Acknowledge(360 * 1024), std::optional<bool>{true});
    ASSERT_TRUE(window.Reserve(FileSendWindow::kMaximumMessage));
    EXPECT_FALSE(window.Reserve(1));
    EXPECT_FALSE(window.Reserve(FileSendWindow::kMaximumMessage + 1));
}

class FileReceiptTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = Endpoint::Bind("{}", 5000);
        client_ = Endpoint::Bind("{}", 5000);
        ASSERT_TRUE(server_);
        ASSERT_TRUE(client_);
        const auto address = server_->Address();
        ASSERT_TRUE(address);
        auto accepted = std::async(std::launch::async, [server = server_] { return server->Accept(5000); });
        outgoing_ = client_->Connect(*address, 5000);
        incoming_ = accepted.get();
        ASSERT_TRUE(outgoing_);
        ASSERT_TRUE(incoming_);
        for (const auto kind : {ChannelKind::kControl, ChannelKind::kInput, ChannelKind::kFile}) {
            const auto opened = Channel::Open(outgoing_, kind, 5000);
            ASSERT_TRUE(opened);
            const auto received = Channel::Accept(incoming_, 5000);
            ASSERT_TRUE(received);
            send_channels_.emplace(kind, *opened);
            receive_channels_.emplace(kind, *received);
        }
        sender_ = std::make_shared<MessageSession>(outgoing_, send_channels_, MessageSessionCallbacks{});
        ASSERT_TRUE(sender_->Start());
        Message file{};
        file.set_type(kFileResponse);
        file.mutable_file_response()->mutable_block()->set_data(std::string(120 * 1024, 'f'));
        file_payload_ = Data::From(file.SerializeAsString());
    }
    void TearDown() override {
        if (sender_) sender_->Stop();
        if (receiver_) receiver_->Stop();
        if (outgoing_) outgoing_->Close();
        if (incoming_) incoming_->Close();
        if (client_) client_->Close();
        if (server_) server_->Close();
    }
    std::shared_ptr<Endpoint> server_{};
    std::shared_ptr<Endpoint> client_{};
    std::shared_ptr<Connection> outgoing_{};
    std::shared_ptr<Connection> incoming_{};
    SessionChannels send_channels_{};
    SessionChannels receive_channels_{};
    std::shared_ptr<MessageSession> sender_{};
    std::shared_ptr<MessageSession> receiver_{};
    std::shared_ptr<Data> file_payload_{};
};

TEST_F(FileReceiptTest, CompletedLocalWritesDoNotReleaseFileCreditButInputAndMediaContinue) {
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    const auto first_file = receive_channels_.at(ChannelKind::kFile)->Receive(2000);
    const auto second_file = receive_channels_.at(ChannelKind::kFile)->Receive(2000);
    ASSERT_TRUE(first_file);
    ASSERT_TRUE(second_file);
    // Both local writes and remote reads completed; only the explicit receipt grants credit.
    const auto busy = sender_->SendFile(file_payload_);
    ASSERT_EQ(busy.status(), FileTransferSendStatus::kBusy);
    ASSERT_TRUE(busy.writable_signal());
    EXPECT_EQ(busy.writable_signal()->outcome(), FileTransferWritableOutcome::kPending);
    Message input{};
    input.set_type(kFocusOutEvent);
    sender_->Send(Data::From(input.SerializeAsString()));
    ASSERT_TRUE(receive_channels_.at(ChannelKind::kInput)->Receive(1000));
    ASSERT_TRUE(sender_->SendDatagram(Bytes{3, 1, 4}));
    const auto media = incoming_->ReceiveDatagram(1000);
    ASSERT_TRUE(media);
    EXPECT_EQ(*media, (Bytes{3, 1, 4}));
    const auto writable = std::make_shared<std::promise<FileTransferWritableOutcome>>();
    auto ready = writable->get_future();
    busy.writable_signal()->Subscribe([writable](FileTransferWritableOutcome outcome) { writable->set_value(outcome); });
    ASSERT_TRUE(receive_channels_.at(ChannelKind::kControl)->SendFileReceipt(file_payload_->Size(), 1000));
    ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(ready.get(), FileTransferWritableOutcome::kWritable);
    EXPECT_TRUE(sender_->SendFile(file_payload_).accepted());
}

TEST_F(FileReceiptTest, ReceiptWakeupCanStopSessionAndRepeatedStopClosesPendingWork) {
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    const auto busy = sender_->SendFile(file_payload_);
    ASSERT_EQ(busy.status(), FileTransferSendStatus::kBusy);
    const auto stopped = std::make_shared<std::promise<void>>();
    auto finished = stopped->get_future();
    busy.writable_signal()->Subscribe([owner = std::weak_ptr<MessageSession>{sender_}, stopped](FileTransferWritableOutcome outcome) {
        if (const auto session = owner.lock()) {
            session->Stop();
            session->Stop();
        }
        stopped->set_value();
    });
    ASSERT_TRUE(receive_channels_.at(ChannelKind::kControl)->SendFileReceipt(file_payload_->Size(), 1000));
    ASSERT_EQ(finished.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(sender_->IsAlive());
    EXPECT_EQ(sender_->SendFile(file_payload_).status(), FileTransferSendStatus::kDisconnected);
    EXPECT_FALSE(sender_->Start());
}

TEST_F(FileReceiptTest, RealReceiverIssuesReceiptsAndBothDirectionsMakeProgress) {
    const auto arrived = std::make_shared<std::promise<void>>();
    auto delivered = arrived->get_future();
    const auto received_count = std::make_shared<std::atomic_size_t>();
    receiver_ = std::make_shared<MessageSession>(
        incoming_, receive_channels_, MessageSessionCallbacks{.message = [arrived, received_count](ChannelKind kind, std::shared_ptr<Data> payload) {
            if (kind == ChannelKind::kFile && ++*received_count == 12) arrived->set_value();
        }});
    ASSERT_TRUE(receiver_->Start());
    for (std::size_t message_index{}; message_index < 12;) {
        const auto sent = sender_->SendFile(file_payload_);
        if (sent.accepted()) {
            ++message_index;
            continue;
        }
        ASSERT_EQ(sent.status(), FileTransferSendStatus::kBusy);
        const auto writable = std::make_shared<std::promise<FileTransferWritableOutcome>>();
        auto ready = writable->get_future();
        sent.writable_signal()->Subscribe([writable](FileTransferWritableOutcome outcome) { writable->set_value(outcome); });
        ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
        ASSERT_EQ(ready.get(), FileTransferWritableOutcome::kWritable);
    }
    ASSERT_EQ(delivered.wait_for(2s), std::future_status::ready);
    for (std::size_t reply_index{}; reply_index < 12;) {
        const auto sent = receiver_->SendFile(file_payload_);
        if (sent.accepted()) {
            ++reply_index;
            continue;
        }
        ASSERT_EQ(sent.status(), FileTransferSendStatus::kBusy);
        const auto writable = std::make_shared<std::promise<FileTransferWritableOutcome>>();
        auto ready = writable->get_future();
        sent.writable_signal()->Subscribe([writable](FileTransferWritableOutcome outcome) { writable->set_value(outcome); });
        ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
        ASSERT_EQ(ready.get(), FileTransferWritableOutcome::kWritable);
    }
    receiver_->Stop();
    receiver_->Stop();
}

TEST_F(FileReceiptTest, StopClosesCreditWaitAndQueuedCompletionsRemainSafe) {
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    ASSERT_TRUE(sender_->SendFile(file_payload_).accepted());
    const auto busy = sender_->SendFile(file_payload_);
    ASSERT_EQ(busy.status(), FileTransferSendStatus::kBusy);
    sender_->Stop();
    sender_->Stop();
    sender_.reset();
    EXPECT_EQ(busy.writable_signal()->outcome(), FileTransferWritableOutcome::kClosed);
}
}  // namespace
}  // namespace px::transport
