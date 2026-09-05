#include <gtest/gtest.h>

#include <memory>

#include "network/network_transport_hub.h"
#include "px_common/data.h"

namespace px::render {
namespace {

TEST(NetworkTransportHubTest, ExactRouteIsForwardedWithoutBroadcast) {
    auto control_calls = std::make_shared<std::size_t>(0);
    auto file_calls = std::make_shared<std::size_t>(0);
    const auto hub = NetworkTransportHub::Create(
        [control_calls](const TransportRoute& route,
                        const std::shared_ptr<Data>& message,
                        const bool run_through) {
            ++*control_calls;
            return route.transport_id == "rtc-local" &&
                   route.stream_id == "stream-a" && message && run_through;
        },
        [file_calls](const TransportRoute& route,
                     const std::shared_ptr<Data>& message) {
            ++*file_calls;
            return route.transport_id == "relay" &&
                   route.connection_id == "connection-a" && message
                ? FileTransferSendResult::Accepted()
                : FileTransferSendResult::Disconnected("route mismatch");
        });
    const auto payload = Data::From("payload");

    EXPECT_TRUE(hub->SendControl(
        {.transport_id = "rtc-local", .stream_id = "stream-a"},
        payload, true));
    EXPECT_TRUE(hub->SendFileTransfer(
        {.channel = TransportChannelKind::kFileTransfer,
         .transport_id = "relay",
         .connection_id = "connection-a",
         .stream_id = "stream-b"},
        payload).accepted());
    EXPECT_EQ(*control_calls, 1U);
    EXPECT_EQ(*file_calls, 1U);
    const auto snapshot = hub->Snapshot();
    EXPECT_EQ(snapshot.control_attempts, 1U);
    EXPECT_EQ(snapshot.control_failures, 0U);
    EXPECT_EQ(snapshot.file_transfer_attempts, 1U);
    EXPECT_EQ(snapshot.file_transfer_failures, 0U);
}

TEST(NetworkTransportHubTest, InvalidPayloadFailsBeforeTransportCallback) {
    auto calls = std::make_shared<std::size_t>(0);
    const auto hub = NetworkTransportHub::Create(
        [calls](const TransportRoute&, const std::shared_ptr<Data>&, bool) {
            ++*calls;
            return true;
        },
        [calls](const TransportRoute&, const std::shared_ptr<Data>&) {
            ++*calls;
            return FileTransferSendResult::Accepted();
        });

    EXPECT_FALSE(hub->SendControl({}, {}));
    EXPECT_FALSE(hub->SendFileTransfer({}, {}).accepted());
    EXPECT_EQ(*calls, 0U);
    const auto snapshot = hub->Snapshot();
    EXPECT_EQ(snapshot.control_failures, 1U);
    EXPECT_EQ(snapshot.file_transfer_failures, 1U);
}

}  // namespace
}  // namespace px::render
