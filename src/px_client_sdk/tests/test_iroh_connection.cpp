#include <gtest/gtest.h>

#include <future>

#include "connection/iroh_connection.h"
#include "px_common/data.h"
#include "px_message.pb.h"

namespace px {
namespace {

TEST(IrohSdkConnection, InvalidChannelInitializationClosesTheAdmittedConnection) {
    const auto server_endpoint = transport::Endpoint::Bind("{}", 5000);
    const auto client_endpoint = transport::Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server_endpoint);
    ASSERT_TRUE(client_endpoint);
    const auto address = server_endpoint->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server_endpoint] { return server_endpoint->Accept(5000); });
    const auto client_connection = client_endpoint->Connect(*address, 5000);
    const auto server_connection = accepted.get();
    ASSERT_TRUE(client_connection);
    ASSERT_TRUE(server_connection);
    const auto adapter = std::make_shared<IrohConnection>(nullptr, client_connection, transport::SessionChannels{});
    const auto failed = std::make_shared<std::atomic_size_t>();
    adapter->RegisterOnDisConnectedCallback([failed] { ++*failed; });
    adapter->Start();
    EXPECT_FALSE(adapter->IsAlive());
    // A partial Start owns and must close the underlying connection even when no worker was created.
    EXPECT_TRUE(client_connection->IsClosed());
    EXPECT_EQ(failed->load(), 1);
    adapter->Stop();
    adapter->Start();
    EXPECT_EQ(failed->load(), 1);
    server_connection->Close();
    client_endpoint->Close();
    server_endpoint->Close();
}

TEST(IrohSdkConnection, FileBackpressureDoesNotLoseBlocksOrBlockControl) {
    const auto server_endpoint = transport::Endpoint::Bind("{}", 5000);
    const auto client_endpoint = transport::Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server_endpoint);
    ASSERT_TRUE(client_endpoint);
    const auto address = server_endpoint->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server_endpoint] { return server_endpoint->Accept(5000); });
    const auto outgoing = client_endpoint->Connect(*address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    transport::SessionChannels channels{};
    transport::SessionChannels peers{};
    for (const auto kind : {transport::ChannelKind::kControl, transport::ChannelKind::kFile}) {
        const auto opened = transport::Channel::Open(outgoing, kind, 5000);
        const auto received = transport::Channel::Accept(incoming, 5000);
        ASSERT_TRUE(opened);
        ASSERT_TRUE(received);
        channels.emplace(kind, *opened);
        peers.emplace(kind, *received);
    }
    const auto adapter = std::make_shared<IrohConnection>(nullptr, outgoing, std::move(channels));
    adapter->Start();
    Message message{};
    message.set_type(kFileResponse);
    message.mutable_file_response()->mutable_block()->set_data(std::string(900 * 1024, 'f'));
    const auto wire = message.SerializeAsString();
    const auto payload = Data::From(wire);
    std::size_t accepted_blocks{};
    std::shared_ptr<FileTransferWritableSignal> writable{};
    for (std::size_t attempt{}; attempt < 256; ++attempt) {
        const auto result = adapter->PostFileTransferMessage(payload);
        if (result.status() == FileTransferSendStatus::kBusy) {
            writable = result.writable_signal();
            break;
        }
        ASSERT_TRUE(result.accepted());
        ++accepted_blocks;
    }
    ASSERT_TRUE(writable);
    ASSERT_GT(accepted_blocks, 0);
    Message heartbeat{};
    heartbeat.set_type(kHeartBeat);
    const auto heartbeat_wire = heartbeat.SerializeAsString();
    adapter->PostBinaryMessage(Data::From(heartbeat_wire));
    const auto control = peers.at(transport::ChannelKind::kControl)->Receive(3000);
    ASSERT_TRUE(control);
    EXPECT_EQ(std::string(control->begin(), control->end()), heartbeat_wire);
    // Drain every accepted block and one retried block; the rejected attempt must never appear on the wire.
    auto drained = std::async(std::launch::async, [channel = peers.at(transport::ChannelKind::kFile),
                                                receipts = peers.at(transport::ChannelKind::kControl), accepted_blocks, wire] {
        for (std::size_t block_index{}; block_index <= accepted_blocks; ++block_index) {
            const auto received = channel->Receive(5000);
            if (!received || std::string(received->begin(), received->end()) != wire) return false;
            if (!receipts->SendFileReceipt((block_index + 1) * wire.size(), 5000)) return false;
        }
        return true;
    });
    bool retried{};
    for (std::size_t attempt{}; attempt < 256 && !retried; ++attempt) {
        const auto wake = std::make_shared<std::promise<FileTransferWritableOutcome>>();
        auto awakened = wake->get_future();
        writable->Subscribe([wake](FileTransferWritableOutcome outcome) { wake->set_value(outcome); });
        ASSERT_EQ(awakened.wait_for(std::chrono::seconds(3)), std::future_status::ready);
        ASSERT_EQ(awakened.get(), FileTransferWritableOutcome::kWritable);
        const auto result = adapter->PostFileTransferMessage(payload);
        retried = result.accepted();
        if (!retried) {
            ASSERT_EQ(result.status(), FileTransferSendStatus::kBusy);
            writable = result.writable_signal();
            ASSERT_TRUE(writable);
        }
    }
    ASSERT_TRUE(retried);
    ASSERT_TRUE(drained.get());
    const auto extra_block = peers.at(transport::ChannelKind::kFile)->Receive(30);
    ASSERT_FALSE(extra_block);
    EXPECT_EQ(extra_block.error(), transport::Error::kTimeout);
    adapter->Stop();
    EXPECT_EQ(adapter->PostFileTransferMessage(payload).status(), FileTransferSendStatus::kDisconnected);
    incoming->Close();
    client_endpoint->Close();
    server_endpoint->Close();
}

TEST(IrohSdkConnection, ReliableDeliveryAndStopFromDatagramCallback) {
    const auto server_endpoint = transport::Endpoint::Bind("{}", 5000);
    const auto client_endpoint = transport::Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server_endpoint);
    ASSERT_TRUE(client_endpoint);
    const auto address = server_endpoint->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server_endpoint] { return server_endpoint->Accept(5000); });
    const auto client_connection = client_endpoint->Connect(*address, 5000);
    const auto server_connection = accepted.get();
    ASSERT_TRUE(client_connection);
    ASSERT_TRUE(server_connection);
    const auto client_channel = transport::Channel::Open(client_connection, transport::ChannelKind::kControl, 5000);
    const auto server_channel = transport::Channel::Accept(server_connection, 5000);
    ASSERT_TRUE(client_channel);
    ASSERT_TRUE(server_channel);
    auto adapter = std::make_shared<IrohConnection>(nullptr, client_connection, *client_channel);
    const auto sent_promise = std::make_shared<std::promise<bool>>();
    auto sent = sent_promise->get_future();
    const auto stopped_promise = std::make_shared<std::promise<void>>();
    auto stopped = stopped_promise->get_future();
    const std::weak_ptr<IrohConnection> weak_adapter = adapter;
    adapter->SetDatagramCallback([weak_adapter, stopped_promise](transport::Bytes payload) {
        EXPECT_EQ(payload, (transport::Bytes{5, 6, 7}));
        if (const auto connection = weak_adapter.lock()) connection->Stop();
        stopped_promise->set_value();
    });
    adapter->Start();
    adapter->PostReliableBinaryMessage(Data::From("iroh-sdk-control"), [sent_promise](bool delivered) { sent_promise->set_value(delivered); });
    const auto received = (*server_channel)->Receive(5000);
    ASSERT_TRUE(received);
    EXPECT_EQ(std::string(received->begin(), received->end()), "iroh-sdk-control");
    ASSERT_EQ(sent.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_TRUE(sent.get());
    ASSERT_TRUE(server_connection->SendDatagram(transport::Bytes{5, 6, 7}));
    ASSERT_EQ(stopped.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_FALSE(adapter->IsAlive());
    adapter->Start();
    EXPECT_FALSE(adapter->IsAlive());
    adapter.reset();
    server_connection->Close();
    client_endpoint->Close();
    server_endpoint->Close();
}

TEST(IrohSdkConnection, BusinessMessagesUseSeparateStreamsAndReceiveCallbackCanStop) {
    const auto server_endpoint = transport::Endpoint::Bind("{}", 5000);
    const auto client_endpoint = transport::Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server_endpoint);
    ASSERT_TRUE(client_endpoint);
    const auto address = server_endpoint->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server_endpoint] { return server_endpoint->Accept(5000); });
    const auto outgoing = client_endpoint->Connect(*address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    transport::SessionChannels client_channels{};
    transport::SessionChannels server_channels{};
    for (const auto kind :
         {transport::ChannelKind::kControl, transport::ChannelKind::kInput, transport::ChannelKind::kClipboard, transport::ChannelKind::kFile}) {
        const auto opened = transport::Channel::Open(outgoing, kind, 5000);
        const auto received = transport::Channel::Accept(incoming, 5000);
        ASSERT_TRUE(opened);
        ASSERT_TRUE(received);
        client_channels.emplace(kind, *opened);
        server_channels.emplace(kind, *received);
    }
    const auto adapter = std::make_shared<IrohConnection>(nullptr, outgoing, std::move(client_channels));
    const auto callback_stopped = std::make_shared<std::promise<void>>();
    auto stopped = callback_stopped->get_future();
    adapter->RegisterOnMessageCallback([owner = std::weak_ptr<IrohConnection>{adapter}, callback_stopped](std::shared_ptr<Data>) {
        if (const auto connection = owner.lock()) connection->Stop();
        callback_stopped->set_value();
    });
    adapter->Start();
    Message file_message{};
    file_message.set_type(kFileResponse);
    file_message.mutable_file_response()->mutable_block()->set_data(std::string(512 * 1024, 'f'));
    adapter->PostBinaryMessage(Data::From(file_message.SerializeAsString()));
    Message input_message{};
    input_message.set_type(kMouseEvent);
    const auto input_wire = input_message.SerializeAsString();
    adapter->PostBinaryMessage(Data::From(input_wire));
    Message control_message{};
    control_message.set_type(kHeartBeat);
    const auto control_wire = control_message.SerializeAsString();
    adapter->PostBinaryMessage(Data::From(control_wire));
    // Do not drain the file stream before input/control: their delivery is independent.
    const auto received_input = server_channels.at(transport::ChannelKind::kInput)->Receive(3000);
    const auto received_control = server_channels.at(transport::ChannelKind::kControl)->Receive(3000);
    ASSERT_TRUE(received_input);
    ASSERT_TRUE(received_control);
    EXPECT_EQ(std::string(received_input->begin(), received_input->end()), input_wire);
    EXPECT_EQ(std::string(received_control->begin(), received_control->end()), control_wire);
    const auto received_file = server_channels.at(transport::ChannelKind::kFile)->Receive(3000);
    ASSERT_TRUE(received_file);
    EXPECT_EQ(std::string(received_file->begin(), received_file->end()), file_message.SerializeAsString());
    const transport::Bytes reply{'r', 'e', 'p', 'l', 'y'};
    ASSERT_TRUE(server_channels.at(transport::ChannelKind::kControl)->Send(reply, 3000));
    ASSERT_EQ(stopped.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_FALSE(adapter->IsAlive());
    incoming->Close();
    client_endpoint->Close();
    server_endpoint->Close();
}

}  // namespace
}  // namespace px
