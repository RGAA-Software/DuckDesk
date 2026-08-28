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

TEST(FileTransferSendResult, SharedQueueLimitHasOneUnambiguousBoundary) {
    EXPECT_EQ(kMaxFileTransferQueuedMessages, 256);
    EXPECT_LT(kMaxFileTransferQueuedMessages - 1,
              kMaxFileTransferQueuedMessages);
    EXPECT_GE(kMaxFileTransferQueuedMessages,
              kMaxFileTransferQueuedMessages);
    EXPECT_EQ(kFileTransferQueueLowWatermark, 64);
}

TEST(FileTransferSendResult, WritableSignalHandlesWakeCloseAndLateSubscription) {
    const auto writable = FileTransferWritableSignal::Create();
    const auto writable_calls = std::make_shared<int>(0);
    writable->Subscribe([writable_calls](FileTransferWritableOutcome outcome) {
        EXPECT_EQ(outcome, FileTransferWritableOutcome::kWritable);
        ++*writable_calls;
    });
    writable->NotifyWritable();
    writable->NotifyWritable();
    writable->Subscribe([writable_calls](FileTransferWritableOutcome outcome) {
        EXPECT_EQ(outcome, FileTransferWritableOutcome::kWritable);
        ++*writable_calls;
    });
    EXPECT_EQ(*writable_calls, 2);

    const auto closed = FileTransferWritableSignal::Create();
    closed->Close();
    const auto closed_calls = std::make_shared<int>(0);
    closed->Subscribe([closed_calls](FileTransferWritableOutcome outcome) {
        EXPECT_EQ(outcome, FileTransferWritableOutcome::kClosed);
        ++*closed_calls;
    });
    EXPECT_EQ(*closed_calls, 1);
}

} // namespace
} // namespace px
