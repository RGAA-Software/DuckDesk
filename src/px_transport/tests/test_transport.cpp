#include <gtest/gtest.h>

#include <future>
#include <nlohmann/json.hpp>
#include <thread>

#include "channel.h"
#include "endpoint_configuration.h"
#include "transport.h"

namespace px::transport {
namespace {

TEST(IrohTransport, CandidateAdditionAfterStartingWithoutRelays) {
    auto configuration = nlohmann::json::parse(testing::EndpointConfiguration());
    const auto candidates = configuration.value("relays", nlohmann::json::array());
    if (candidates.empty()) GTEST_SKIP() << "requires a private test Relay";
    configuration["relays"] = nlohmann::json::array();
    configuration["relay_only"] = false;
    const auto server = Endpoint::Bind(configuration.dump(), 5000);
    ASSERT_TRUE(server);
    ASSERT_TRUE(server->UpdateRelays(candidates.dump()));
    std::string relay_address{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto address = server->Address();
        ASSERT_TRUE(address);
        auto endpoint_address = nlohmann::json::parse(*address);
        for (const auto& route : endpoint_address["addrs"]) {
            if (route.contains("Relay")) {
                endpoint_address["addrs"] = nlohmann::json::array({route});
                relay_address = endpoint_address.dump();
                break;
            }
        }
        if (!relay_address.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_FALSE(relay_address.empty());
    configuration["relays"] = candidates;
    configuration["relay_only"] = true;
    const auto client = Endpoint::Bind(configuration.dump(), 5000);
    ASSERT_TRUE(client);
    auto accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
    const auto outgoing = client->Connect(relay_address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    const Bytes payload{2, 4, 6};
    ASSERT_TRUE(outgoing->SendDatagram(payload));
    const auto received = incoming->ReceiveDatagram(5000);
    ASSERT_TRUE(received);
    EXPECT_EQ(*received, payload);
    EXPECT_EQ(outgoing->Snapshot().path, PathKind::kRelay);
    const auto selected_path = outgoing->SelectedPath();
    ASSERT_TRUE(selected_path);
    const auto selected_address = nlohmann::json::parse(*selected_path);
    ASSERT_TRUE(selected_address.contains("Relay"));
    EXPECT_EQ(selected_address["Relay"], nlohmann::json::parse(relay_address)["addrs"][0]["Relay"]);
    outgoing->Close();
    incoming->Close();
    client->Close();
    server->Close();
}

TEST(IrohTransport, CandidateReplacementRetainsHeldStreamsAndServesNewPeers) {
    auto configuration = nlohmann::json::parse(testing::EndpointConfiguration());
    const auto candidates = configuration.value("relays", nlohmann::json::array());
    if (candidates.size() < 2) GTEST_SKIP() << "requires at least two private test Relays";
    configuration["relay_only"] = true;
    configuration["relays"] = nlohmann::json::array({candidates[0]});
    const auto server = Endpoint::Bind(configuration.dump(), 5000);
    const auto client = Endpoint::Bind(configuration.dump(), 5000);
    ASSERT_TRUE(server);
    ASSERT_TRUE(client);
    const auto original_address = server->Address();
    ASSERT_TRUE(original_address);
    auto accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
    const auto outgoing = client->Connect(*original_address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    const Bytes payload{1, 3, 5, 7};
    const auto send_stream = outgoing->OpenStream(100, 5000);
    ASSERT_TRUE(send_stream);
    ASSERT_TRUE(send_stream->Write(payload, 5000));
    const auto receive_stream = incoming->AcceptStream(5000);
    ASSERT_TRUE(receive_stream);
    ASSERT_TRUE(receive_stream->Read(1024, 5000));
    const auto replacement = nlohmann::json::array({candidates[1]}).dump();
    ASSERT_TRUE(server->UpdateRelays(replacement));
    ASSERT_TRUE(client->UpdateRelays(replacement));
    // Reapplying the same policy must not restart either endpoint.
    ASSERT_TRUE(server->UpdateRelays(replacement));
    bool home_changed{};
    std::string refreshed_address{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto address = server->Address();
        ASSERT_TRUE(address);
        const auto endpoint_address = nlohmann::json::parse(*address);
        EXPECT_EQ(endpoint_address["id"], nlohmann::json::parse(*original_address)["id"]);
        for (const auto& route : endpoint_address["addrs"]) {
            if (route.value("Relay", std::string{}) == candidates[1]["url"].get<std::string>()) {
                home_changed = true;
                refreshed_address = *address;
            }
        }
        if (home_changed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(home_changed);
    ASSERT_TRUE(receive_stream->Write(payload, 5000));
    const auto reply = send_stream->Read(1024, 5000);
    ASSERT_TRUE(reply);
    EXPECT_EQ(*reply, payload);
    ASSERT_TRUE(outgoing->SendDatagram(payload));
    const auto datagram = incoming->ReceiveDatagram(5000);
    ASSERT_TRUE(datagram);
    EXPECT_EQ(*datagram, payload);
    configuration["relays"] = nlohmann::json::array({candidates[1]});
    const auto newcomer = Endpoint::Bind(configuration.dump(), 5000);
    ASSERT_TRUE(newcomer);
    auto next_accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
    const auto next_outgoing = newcomer->Connect(refreshed_address, 5000);
    const auto next_incoming = next_accepted.get();
    ASSERT_TRUE(next_outgoing);
    ASSERT_TRUE(next_incoming);
    ASSERT_FALSE(outgoing->IsClosed());
    next_outgoing->Close();
    next_incoming->Close();
    outgoing->Close();
    incoming->Close();
    newcomer->Close();
    client->Close();
    server->Close();
}

TEST(IrohTransport, ReliableStreamAndDatagramShareAnAuthenticatedConnection) {
    const auto server = Endpoint::Bind("{}", 5000);
    const auto client = Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server);
    ASSERT_TRUE(client);
    const auto address = server->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
    const auto outgoing = client->Connect(*address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    const auto send_stream = outgoing->OpenStream(100, 5000);
    ASSERT_TRUE(send_stream);
    const Bytes payload{1, 4, 9, 16, 25};
    ASSERT_TRUE(send_stream->Write(payload, 5000));
    const auto receive_stream = incoming->AcceptStream(5000);
    ASSERT_TRUE(receive_stream);
    const auto received = receive_stream->Read(1024, 5000);
    ASSERT_TRUE(received);
    EXPECT_EQ(*received, payload);
    ASSERT_TRUE(send_stream->Finish(5000));
    const auto end_of_stream = receive_stream->Read(1024, 5000);
    ASSERT_FALSE(end_of_stream);
    EXPECT_EQ(end_of_stream.error(), Error::kClosed);
    // FIN in one direction must preserve the reverse direction (required by RDP).
    ASSERT_TRUE(receive_stream->Write(payload, 5000));
    const auto response = send_stream->Read(1024, 5000);
    ASSERT_TRUE(response);
    EXPECT_EQ(*response, payload);
    ASSERT_GT(outgoing->DatagramLimit(), payload.size());
    ASSERT_TRUE(outgoing->SendDatagram(payload));
    const auto datagram = incoming->ReceiveDatagram(5000);
    ASSERT_TRUE(datagram);
    EXPECT_EQ(*datagram, payload);
    const auto snapshot = incoming->Snapshot();
    EXPECT_EQ(snapshot.path, PathKind::kDirect);
    EXPECT_GT(snapshot.open_paths, 0);
    EXPECT_GT(snapshot.received_packets, 0);
    EXPECT_GT(snapshot.received_datagrams, 0);
    outgoing->Close();
    incoming->Close();
    client->Close();
    server->Close();
}

TEST(IrohTransport, CloseInterruptsPendingReceiveAndRepeatedEndpointLifetimes) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto server = Endpoint::Bind("{}", 5000);
        const auto client = Endpoint::Bind("{}", 5000);
        ASSERT_TRUE(server);
        ASSERT_TRUE(client);
        const auto address = server->Address();
        ASSERT_TRUE(address);
        auto accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
        const auto outgoing = client->Connect(*address, 5000);
        const auto incoming = accepted.get();
        ASSERT_TRUE(outgoing);
        ASSERT_TRUE(incoming);
        auto pending_receive = std::async(std::launch::async, [incoming] { return incoming->ReceiveDatagram(10000); });
        incoming->Close();
        ASSERT_EQ(pending_receive.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_FALSE(pending_receive.get());
        outgoing->Close();
        client->Close();
        server->Close();
    }
}

TEST(IrohTransport, IndependentBusinessChannelsPreserveMessageBoundaries) {
    const auto server = Endpoint::Bind("{}", 5000);
    const auto client = Endpoint::Bind("{}", 5000);
    ASSERT_TRUE(server);
    ASSERT_TRUE(client);
    const auto address = server->Address();
    ASSERT_TRUE(address);
    auto accepted = std::async(std::launch::async, [server] { return server->Accept(5000); });
    const auto outgoing = client->Connect(*address, 5000);
    const auto incoming = accepted.get();
    ASSERT_TRUE(outgoing);
    ASSERT_TRUE(incoming);
    for (const auto kind : {ChannelKind::kControl, ChannelKind::kInput, ChannelKind::kClipboard, ChannelKind::kFile, ChannelKind::kRdp}) {
        const auto sender = Channel::Open(outgoing, kind, 5000);
        ASSERT_TRUE(sender);
        const auto receiver = Channel::Accept(incoming, 5000);
        ASSERT_TRUE(receiver);
        EXPECT_EQ((*receiver)->Kind(), kind);
        const auto forward_priority = (*sender)->SendingPriority(1000);
        const auto return_priority = (*receiver)->SendingPriority(1000);
        ASSERT_TRUE(forward_priority);
        ASSERT_TRUE(return_priority);
        EXPECT_EQ(*return_priority, *forward_priority);
        if (kind == ChannelKind::kFile) EXPECT_LT(*return_priority, 0);
        if (kind == ChannelKind::kInput) EXPECT_GT(*return_priority, 0);
        const Bytes payload(65536, static_cast<std::uint8_t>(kind));
        ASSERT_TRUE((*sender)->Send(payload, 5000));
        ASSERT_TRUE((*sender)->Send(Bytes{9, 8, 7}, 5000));
        const auto first_message = (*receiver)->Receive(5000);
        const auto second_message = (*receiver)->Receive(5000);
        ASSERT_TRUE(first_message);
        ASSERT_TRUE(second_message);
        EXPECT_EQ(*first_message, payload);
        EXPECT_EQ(*second_message, (Bytes{9, 8, 7}));
        ASSERT_TRUE((*receiver)->Send(payload, 5000));
        const auto reply = (*sender)->Receive(5000);
        ASSERT_TRUE(reply);
        EXPECT_EQ(*reply, payload);
    }
    // A timed-out read may already have consumed part of a message. Preserve framing for the next read.
    const auto fragmented_stream = outgoing->OpenStream(100, 5000);
    ASSERT_TRUE(fragmented_stream);
    ASSERT_TRUE(fragmented_stream->Write(Bytes{'P', 'X', 'Q', 2, 1, 0, 0, 0}, 5000));
    const auto fragmented_channel = Channel::Accept(incoming, 5000);
    ASSERT_TRUE(fragmented_channel);
    ASSERT_TRUE(fragmented_stream->Write(Bytes{0, 0}, 5000));
    const auto partial_header = (*fragmented_channel)->Receive(30);
    ASSERT_FALSE(partial_header);
    EXPECT_EQ(partial_header.error(), Error::kTimeout);
    ASSERT_TRUE(fragmented_stream->Write(Bytes{0, 3, 11}, 5000));
    const auto partial_body = (*fragmented_channel)->Receive(30);
    ASSERT_FALSE(partial_body);
    EXPECT_EQ(partial_body.error(), Error::kTimeout);
    ASSERT_TRUE(fragmented_stream->Write(Bytes{22, 33}, 5000));
    const auto completed = (*fragmented_channel)->Receive(5000);
    ASSERT_TRUE(completed);
    EXPECT_EQ(*completed, (Bytes{11, 22, 33}));
    const auto receipts = std::make_shared<std::vector<std::uint64_t>>();
    (*fragmented_channel)->SetFileReceiptHandler([receipts](std::uint64_t received_bytes) {
        receipts->push_back(received_bytes);
        return received_bytes == 17;
    });
    ASSERT_TRUE(fragmented_stream->Write(Bytes{0, 0, 0, 0, 0, 0}, 5000));
    const auto partial_receipt = (*fragmented_channel)->Receive(30);
    ASSERT_FALSE(partial_receipt);
    EXPECT_EQ(partial_receipt.error(), Error::kTimeout);
    ASSERT_TRUE(fragmented_stream->Write(Bytes{0, 0, 0, 0, 0, 17, 0, 0, 0, 1, 99}, 5000));
    const auto after_receipt = (*fragmented_channel)->Receive(5000);
    ASSERT_TRUE(after_receipt);
    EXPECT_EQ(*after_receipt, (Bytes{99}));
    EXPECT_EQ(*receipts, (std::vector<std::uint64_t>{17}));
    ASSERT_TRUE(fragmented_stream->Write(Bytes{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 18}, 5000));
    const auto invalid_receipt = (*fragmented_channel)->Receive(5000);
    ASSERT_FALSE(invalid_receipt);
    EXPECT_EQ(invalid_receipt.error(), Error::kInvalid);
    incoming->Close();
    outgoing->Close();
    client->Close();
    server->Close();
}

}  // namespace
}  // namespace px::transport
