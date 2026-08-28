#include <gtest/gtest.h>

#include "px_common_new/file_transfer_send_result.h"

namespace px {
namespace {

TEST(FileTransferSendResult, DistinguishesAcceptedBusyAndTerminalFailures) {
    const auto accepted = FileTransferSendResult::Accepted();
    const auto busy = FileTransferSendResult::Busy("queue full");
    const auto disconnected = FileTransferSendResult::Disconnected("channel closed");
    const auto failed = FileTransferSendResult::TransportError("serialization failed");

    EXPECT_TRUE(accepted.accepted());
    EXPECT_EQ(accepted.status(), FileTransferSendStatus::kAccepted);

    EXPECT_FALSE(busy.accepted());
    EXPECT_EQ(busy.status(), FileTransferSendStatus::kBusy);
    EXPECT_EQ(busy.detail(), "queue full");

    EXPECT_FALSE(disconnected.accepted());
    EXPECT_EQ(disconnected.status(), FileTransferSendStatus::kDisconnected);
    EXPECT_EQ(disconnected.detail(), "channel closed");

    EXPECT_FALSE(failed.accepted());
    EXPECT_EQ(failed.status(), FileTransferSendStatus::kTransportError);
    EXPECT_EQ(failed.detail(), "serialization failed");
}

} // namespace
} // namespace px
