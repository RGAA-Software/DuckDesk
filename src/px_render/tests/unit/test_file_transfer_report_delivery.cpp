#include <gtest/gtest.h>

#include "network/file_transfer_report_delivery.h"

namespace px {
namespace {

TEST(FileTransferReportDelivery, RetriesAnIdenticalPendingSnapshotUntilAccepted) {
    FileTransferReportDelivery delivery;
    delivery.RecordProgress(512);

    const auto first_attempt = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(first_attempt);
    EXPECT_EQ(first_attempt->sequence, 1U);
    EXPECT_EQ(first_attempt->transferred_bytes, 512U);

    delivery.RecordProgress(1024);
    const auto retry = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry->sequence, first_attempt->sequence);
    EXPECT_EQ(retry->transferred_bytes, first_attempt->transferred_bytes);

    EXPECT_TRUE(delivery.Accept(retry->sequence));
    const auto next_progress = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(next_progress);
    EXPECT_EQ(next_progress->sequence, 2U);
    EXPECT_EQ(next_progress->transferred_bytes, 1024U);
}

TEST(FileTransferReportDelivery, SendsTerminalStateAfterPendingProgressIsAccepted) {
    FileTransferReportDelivery delivery;
    delivery.RecordProgress(400);
    const auto pending_progress = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(pending_progress);

    std::array<std::uint8_t, 32> digest{};
    digest[0] = 0x7a;
    delivery.RecordTerminal(800, 2, digest);

    const auto retry = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(retry);
    EXPECT_FALSE(retry->terminal);
    EXPECT_EQ(retry->transferred_bytes, 400U);
    ASSERT_TRUE(delivery.Accept(retry->sequence));

    const auto terminal = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(terminal);
    EXPECT_TRUE(terminal->terminal);
    EXPECT_EQ(terminal->sequence, 2U);
    EXPECT_EQ(terminal->transferred_bytes, 800U);
    EXPECT_EQ(terminal->outcome, 2);
    ASSERT_TRUE(terminal->verified_sha256);
    EXPECT_EQ((*terminal->verified_sha256)[0], 0x7a);
}

TEST(FileTransferReportDelivery, RejectsStaleAcknowledgements) {
    FileTransferReportDelivery delivery;
    const auto pending = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(pending);
    EXPECT_FALSE(delivery.Accept(pending->sequence + 1));

    const auto retry = delivery.PrepareSnapshot(1);
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry->sequence, pending->sequence);
}

}  // namespace
}  // namespace px
